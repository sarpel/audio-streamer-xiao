# Start the kimi-k2-turbo-preview model on Windows Powershell
$env:ANTHROPIC_BASE_URL="https://api.moonshot.ai/anthropic";
$env:ANTHROPIC_AUTH_TOKEN="sk-W0ZkD6eo7sDvKbymZ5COWrDL7B9wQbGWdDNvywdYCotY3gmT"
$env:ANTHROPIC_MODEL="kimi-k2-0905-preview"
$env:ANTHROPIC_SMALL_FAST_MODEL="kimi-k2-0905-preview"
claude --dangerously-skip-permissions