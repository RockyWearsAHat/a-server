let currentPath = "/";

function setStatus(msg) {
  const el = document.getElementById("status");
  el.textContent = msg || "";
}

function qs(sel) {
  return document.querySelector(sel);
}

function fmtSize(bytes) {
  if (bytes == null) return "";
  const b = Number(bytes);
  if (!Number.isFinite(b)) return "";
  if (b < 1024) return `${b} B`;
  const units = ["KB", "MB", "GB", "TB"];
  let v = b;
  let i = -1;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i++;
  }
  return `${v.toFixed(v >= 10 ? 0 : 1)} ${units[i]}`;
}

function fmtTime(secs) {
  if (!secs) return "";
  const d = new Date(secs * 1000);
  return d.toLocaleString();
}

function joinPath(base, name) {
  if (base.endsWith("/")) return base + name;
  return base + "/" + name;
}

function parentPath(p) {
  if (p === "/" || !p) return "/";
  const parts = p.split("/").filter(Boolean);
  parts.pop();
  return "/" + parts.join("/");
}

async function apiList(path) {
  const r = await fetch(`/api/list?path=${encodeURIComponent(path)}`);
  if (!r.ok) throw new Error(await r.text());
  return await r.json();
}

async function apiPost(url, bodyObj) {
  const r = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(bodyObj),
  });
  if (!r.ok && r.status !== 204) throw new Error(await r.text());
}

async function apiUpload(dir, file) {
  const r = await fetch(
    `/api/upload?dir=${encodeURIComponent(dir)}&name=${encodeURIComponent(
      file.name
    )}`,
    {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    }
  );
  if (!r.ok) throw new Error(await r.text());
}

function fileUrl(path) {
  return `/file?path=${encodeURIComponent(path)}`;
}

async function showPreview(entry) {
  const title = document.getElementById("previewTitle");
  const box = document.getElementById("preview");
  const openLink = document.getElementById("openLink");

  if (!entry) {
    title.textContent = "Preview";
    box.innerHTML = "";
    openLink.removeAttribute("href");
    openLink.style.visibility = "hidden";
    return;
  }

  const path = joinPath(currentPath, entry.name);
  title.textContent = entry.name;
  openLink.href = fileUrl(path);
  openLink.style.visibility = "visible";

  if (entry.isDir) {
    box.innerHTML = "<div>Folder selected.</div>";
    return;
  }

  const ext = (entry.name.split(".").pop() || "").toLowerCase();
  const url = fileUrl(path);

  if (["png", "jpg", "jpeg", "gif", "webp", "svg"].includes(ext)) {
    box.innerHTML = "";
    const img = document.createElement("img");
    img.src = url;
    img.alt = entry.name;
    box.appendChild(img);
    return;
  }

  if (["mp4", "webm"].includes(ext)) {
    box.innerHTML = "";
    const v = document.createElement("video");
    v.controls = true;
    v.src = url;
    box.appendChild(v);
    return;
  }

  if (["mp3", "wav", "ogg"].includes(ext)) {
    box.innerHTML = "";
    const a = document.createElement("audio");
    a.controls = true;
    a.src = url;
    box.appendChild(a);
    return;
  }

  if (ext === "pdf") {
    box.innerHTML = "";
    const iframe = document.createElement("iframe");
    iframe.src = url;
    iframe.style.width = "100%";
    iframe.style.height = "70vh";
    box.appendChild(iframe);
    return;
  }

  // Default: text preview (best-effort)
  box.innerHTML = "<div>Loading preview…</div>";
  try {
    const r = await fetch(
      `/api/text?path=${encodeURIComponent(path)}&max=262144`
    );
    if (!r.ok) throw new Error(await r.text());
    const t = await r.text();
    box.innerHTML = "";
    const pre = document.createElement("pre");
    pre.textContent = t;
    box.appendChild(pre);
  } catch (e) {
    box.innerHTML = `<div>Preview not available.</div>`;
  }
}

async function refresh() {
  setStatus("Loading…");
  document.getElementById("path").textContent = currentPath;

  let data;
  try {
    data = await apiList(currentPath);
  } catch (e) {
    setStatus(String(e.message || e));
    return;
  }

  const tbody = document.getElementById("tbody");
  tbody.innerHTML = "";

  for (const item of data.items) {
    const tr = document.createElement("tr");

    const tdName = document.createElement("td");
    const nameWrap = document.createElement("div");
    nameWrap.className = "nameCell";
    if (item.isDir) {
      const badge = document.createElement("span");
      badge.className = "badge";
      badge.textContent = "DIR";
      nameWrap.appendChild(badge);
    }
    const a = document.createElement("a");
    a.href = "#";
    a.textContent = item.name;
    a.onclick = (ev) => {
      ev.preventDefault();
      if (item.isDir) {
        currentPath = joinPath(currentPath, item.name);
        showPreview(null);
        refresh();
      } else {
        showPreview(item);
      }
    };
    nameWrap.appendChild(a);
    tdName.appendChild(nameWrap);

    const tdSize = document.createElement("td");
    tdSize.className = "num";
    tdSize.textContent = item.isDir ? "" : fmtSize(item.size);

    const tdTime = document.createElement("td");
    tdTime.className = "num";
    tdTime.textContent = fmtTime(item.mtime);

    const tdAct = document.createElement("td");
    const actions = document.createElement("div");
    actions.className = "actions";

    if (!item.isDir) {
      const open = document.createElement("a");
      open.href = fileUrl(joinPath(currentPath, item.name));
      open.target = "_blank";
      open.rel = "noreferrer";
      open.textContent = "Open";
      actions.appendChild(open);
    }

    const ren = document.createElement("button");
    ren.textContent = "Rename";
    ren.onclick = async () => {
      const newName = prompt("Rename to:", item.name);
      if (!newName || newName === item.name) return;
      try {
        await apiPost("/api/rename", {
          path: joinPath(currentPath, item.name),
          newName,
        });
        showPreview(null);
        refresh();
      } catch (e) {
        alert(String(e.message || e));
      }
    };

    const del = document.createElement("button");
    del.textContent = "Delete";
    del.onclick = async () => {
      const ok = confirm(
        `Delete ${item.isDir ? "folder" : "file"} “${item.name}”?`
      );
      if (!ok) return;
      try {
        await apiPost("/api/delete", {
          path: joinPath(currentPath, item.name),
        });
        showPreview(null);
        refresh();
      } catch (e) {
        alert(String(e.message || e));
      }
    };

    actions.appendChild(ren);
    actions.appendChild(del);
    tdAct.appendChild(actions);

    tr.appendChild(tdName);
    tr.appendChild(tdSize);
    tr.appendChild(tdTime);
    tr.appendChild(tdAct);
    tbody.appendChild(tr);
  }

  setStatus("Ready");
}

function setup() {
  document.getElementById("btnUp").onclick = () => {
    currentPath = parentPath(currentPath);
    showPreview(null);
    refresh();
  };

  document.getElementById("btnRefresh").onclick = () => {
    showPreview(null);
    refresh();
  };

  document.getElementById("btnNewFolder").onclick = async () => {
    const name = prompt("New folder name:");
    if (!name) return;
    try {
      await apiPost("/api/mkdir", { path: currentPath, name });
      refresh();
    } catch (e) {
      alert(String(e.message || e));
    }
  };

  document
    .getElementById("fileInput")
    .addEventListener("change", async (ev) => {
      const files = Array.from(ev.target.files || []);
      if (!files.length) return;
      setStatus(`Uploading ${files.length} file(s)…`);
      try {
        for (const f of files) {
          await apiUpload(currentPath, f);
        }
        ev.target.value = "";
        await refresh();
      } catch (e) {
        alert(String(e.message || e));
        setStatus("Upload failed");
      }
    });

  showPreview(null);
  refresh();
}

setup();
