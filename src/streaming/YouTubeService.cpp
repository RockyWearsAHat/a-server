#include "streaming/YouTubeService.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <curl/curl.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace AIO {
namespace Streaming {

// Helper function for CURL responses
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

YouTubeService::YouTubeService() : authenticated_(false) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

bool YouTubeService::authenticate(const StreamingCredentials& creds) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store API key
    apiKey_ = creds.apiKey;
    
    // If OAuth token provided, use it
    if (!creds.accessToken.empty()) {
        accessToken_ = creds.accessToken;
        authenticated_ = true;
        std::cout << "[YouTube] Authenticated with OAuth token" << std::endl;
        return true;
    }
    
    // For API key only access (public content)
    if (!apiKey_.empty()) {
        authenticated_ = true;
        std::cout << "[YouTube] Authenticated with API key (public access)" << std::endl;
        return true;
    }
    
    return false;
}

bool YouTubeService::isAuthenticated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return authenticated_;
}

void YouTubeService::logout() {
    std::lock_guard<std::mutex> lock(mutex_);
    apiKey_.clear();
    accessToken_.clear();
    authenticated_ = false;
    std::cout << "[YouTube] Logged out" << std::endl;
}

std::string YouTubeService::makeApiRequest(const std::string& endpoint, const std::string& params) {
    CURL* curl = curl_easy_init();
    std::string response;
    
    if (curl) {
        std::string url = "https://www.googleapis.com/youtube/v3/" + endpoint + "?key=" + apiKey_;
        if (!params.empty()) {
            url += "&" + params;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "[YouTube] API request failed: " << curl_easy_strerror(res) << std::endl;
        }
        
        curl_easy_cleanup(curl);
    }
    
    return response;
}

std::vector<VideoContent> YouTubeService::parseVideoResults(const std::string& jsonResponse) {
    std::vector<VideoContent> results;

    if (jsonResponse.empty()) {
        return results;
    }


    const QByteArray bytes(jsonResponse.data(), (int)jsonResponse.size());
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "[YouTube] JSON parse failed: " << err.errorString().toStdString() << std::endl;
        return results;
    }

    const QJsonObject root = doc.object();
    const QJsonArray items = root.value("items").toArray();
    results.reserve((size_t)items.size());

    auto pickThumb = [](const QJsonObject& thumbs) -> QString {
        auto pick = [&](const char* key) -> QString {
            const auto obj = thumbs.value(key).toObject();
            return obj.value("url").toString();
        };
        QString url = pick("maxres");
        if (url.isEmpty()) url = pick("standard");
        if (url.isEmpty()) url = pick("high");
        if (url.isEmpty()) url = pick("medium");
        if (url.isEmpty()) url = pick("default");
        return url;
    };

    for (const auto& v : items) {
        if (!v.isObject()) continue;
        const QJsonObject item = v.toObject();

        VideoContent vc;
        vc.durationSeconds = 0;

        // search.list returns id.videoId; videos.list returns id
        const QJsonValue idVal = item.value("id");
        if (idVal.isObject()) {
            const QJsonObject idObj = idVal.toObject();
            vc.id = idObj.value("videoId").toString().toStdString();
        } else if (idVal.isString()) {
            vc.id = idVal.toString().toStdString();
        }

        const QJsonObject snippet = item.value("snippet").toObject();
        if (!snippet.isEmpty()) {
            vc.title = snippet.value("title").toString().toStdString();
            vc.description = snippet.value("description").toString().toStdString();
            const QJsonObject thumbs = snippet.value("thumbnails").toObject();
            if (!thumbs.isEmpty()) {
                vc.thumbnailUrl = pickThumb(thumbs).toStdString();
            }
        }

        if (!vc.id.empty()) {
            vc.videoUrl = "https://www.youtube.com/watch?v=" + vc.id;
            results.push_back(std::move(vc));
        }
    }

    return results;
}

std::vector<VideoContent> YouTubeService::getTrending(int limit) {
    std::cout << "[YouTube] Fetching trending videos (limit: " << limit << ")" << std::endl;
    
    std::string params = "part=snippet,contentDetails&chart=mostPopular&maxResults=" + std::to_string(limit);
    std::string response = makeApiRequest("videos", params);
    
    return parseVideoResults(response);
}

std::vector<VideoContent> YouTubeService::search(const std::string& query, int limit) {
    std::cout << "[YouTube] Searching for: " << query << " (limit: " << limit << ")" << std::endl;

    // URL-encode query (minimal encoding for safety)
    std::ostringstream enc;
    for (unsigned char c : query) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            enc << c;
        } else if (c == ' ') {
            enc << '+';
        } else {
            enc << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c << std::nouppercase << std::dec;
        }
    }

    std::string params = "part=snippet&type=video&q=" + enc.str() + "&maxResults=" + std::to_string(limit);
    std::string response = makeApiRequest("search", params);
    
    return parseVideoResults(response);
}

std::vector<VideoContent> YouTubeService::getRecommended(int limit) {
    std::cout << "[YouTube] Fetching recommended videos (limit: " << limit << ")" << std::endl;
    
    // YouTube recommendations require OAuth
    if (accessToken_.empty()) {
        std::cout << "[YouTube] OAuth required for recommendations" << std::endl;
        return {};
    }
    
    std::string params = "part=snippet&maxResults=" + std::to_string(limit);
    std::string response = makeApiRequest("activities", params);
    
    return parseVideoResults(response);
}

std::vector<VideoContent> YouTubeService::getContinueWatching() {
    std::cout << "[YouTube] Fetching continue watching list" << std::endl;
    
    // Requires OAuth and watch history API
    if (accessToken_.empty()) {
        std::cout << "[YouTube] OAuth required for watch history" << std::endl;
        return {};
    }
    
    return {};
}

std::string YouTubeService::getStreamUrl(const std::string& contentId) {
    std::cout << "[YouTube] Getting stream URL for: " << contentId << std::endl;
    
    // YouTube uses youtube-dl or yt-dlp for actual stream extraction
    // This would need integration with those tools
    return "https://www.youtube.com/watch?v=" + contentId;
}

bool YouTubeService::startPlayback(const std::string& contentId) {
    std::cout << "[YouTube] Starting playback for: " << contentId << std::endl;
    return true;
}

void YouTubeService::updateWatchProgress(const std::string& contentId, int positionSeconds) {
    std::cout << "[YouTube] Updating watch progress: " << contentId 
              << " @ " << positionSeconds << "s" << std::endl;
}

} // namespace Streaming
} // namespace AIO
