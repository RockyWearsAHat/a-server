# GitHub Copilot Configuration

This directory contains GitHub Copilot instructions, agents, and prompts to guide AI assistance for this project.

## Structure

```
.github/
├── instructions/     # Coding standards and best practices
├── agents/          # Specialized AI assistants
├── prompts/         # Reusable prompt templates
└── copilot/         # Project-specific Copilot context (optional)
```

## Instructions

Instructions define coding standards, best practices, and guidelines that GitHub Copilot should follow when generating code.

### Installed Instructions

- **cmake-vcpkg.instructions.md** - C++ build system and dependency management
- **self-explanatory-code-commenting.instructions.md** - Code documentation philosophy
- **performance-optimization.instructions.md** - Performance best practices
- **object-calisthenics.instructions.md** - Clean code principles
- **security-and-owasp.instructions.md** - Security guidelines

## Agents

Agents are specialized AI assistants that provide expert-level guidance in specific domains.

### Installed Agents

- **expert-cpp-software-engineer.agent.md** - Expert C++ development guidance

## How It Works

1. **Automatic Application**: GitHub Copilot automatically reads these files and applies their guidance
2. **File Patterns**: Each instruction has `applyTo` patterns defining which files it applies to
3. **Priority**: More specific patterns take precedence over general ones
4. **Agents**: Invoke agents explicitly using `@expert-cpp-software-engineer` or similar commands

## Customization

You can customize or add your own:

1. Create new `.instructions.md` files in the `instructions/` directory
2. Create new `.agent.md` files in the `agents/` directory
3. Add `applyTo` patterns to scope when guidelines apply

## Documentation

See [AWESOME_COPILOT_INSTALLED.md](AWESOME_COPILOT_INSTALLED.md) for details on what was installed and why.

## Sources

These files are from the [awesome-copilot](https://github.com/jlorich/awesome-copilot) community repository, providing battle-tested patterns and practices.
