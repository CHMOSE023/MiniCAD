@AGENTS.md

## Claude Code

- 项目 skill 放在 `.agents/skills/`（与其他 AI 工具共用）。Claude Code 只从 `.claude/skills/` 发现 skill，`.claude/` 又被 git 忽略，所以每台机器建一次目录联接：`New-Item -ItemType Junction -Path .claude\skills -Target .agents\skills`（PowerShell，在仓库根目录运行）
