#!/usr/bin/env python3
"""
Script to add Basic Authentication check to all API handlers in web_server.cpp
"""

import re

# Read the file
with open('src/modules/web_server.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Pattern to match handler function definitions
# Matches: static esp_err_t api_*_handler(httpd_req_t *req) {
handler_pattern = r'(static esp_err_t (api_[a-z_]+_handler)\(httpd_req_t \*req\) \{\n)'

# Auth check code to insert
auth_check = '''    // Check authentication
    if (!check_basic_auth(req)) {
        return send_auth_required(req);
    }
    
'''

# Find all handlers and check if they already have auth
matches = list(re.finditer(handler_pattern, content))
print(f"Found {len(matches)} handlers")

modified = False
for match in matches:
    handler_name = match.group(2)
    start_pos = match.end()
    
    # Check if auth check already exists (look at next 200 chars)
    next_chars = content[start_pos:start_pos+200]
    
    if 'check_basic_auth' in next_chars:
        print(f"✓ {handler_name} - already has auth")
    else:
        print(f"+ {handler_name} - adding auth")
        # Insert auth check
        content = content[:start_pos] + auth_check + content[start_pos:]
        modified = True

if modified:
    # Write back
    with open('src/modules/web_server.cpp', 'w', encoding='utf-8') as f:
        f.write(content)
    print("\n✅ File updated successfully!")
else:
    print("\n✓ All handlers already have authentication")
