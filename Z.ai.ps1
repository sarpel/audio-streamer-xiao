# Start the glm4.6 model on Windows Powershell
$env:ANTHROPIC_BASE_URL="https://api.z.ai/api/anthropic";
$env:ANTHROPIC_AUTH_TOKEN="7357023cfa1240bebb3fe4514f97ae8c.Rmsk0azUFR5DBzlT"
$env:ANTHROPIC_MODEL="GLM-4.6"
$env:ANTHROPIC_SMALL_FAST_MODEL="GLM-4.6"
claude --dangerously-skip-permissions