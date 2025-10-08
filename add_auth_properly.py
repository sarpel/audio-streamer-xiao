import re

# Read the file
with open('src/modules/web_server.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Define the auth check code to insert
auth_check = """    // Check authentication
    if (!check_basic_auth(req)) {
        return send_auth_required(req);
    }
    
"""

# List of handlers that need auth (all api_* handlers except OTA handlers which have their own auth)
handlers = [
    'api_get_wifi_handler',
    'api_post_wifi_handler',
    'api_get_tcp_handler',
    'api_post_tcp_handler',
    'api_get_status_handler',
    'api_get_info_handler',
    'api_post_restart_handler',
    'api_post_factory_reset_handler',
    'api_get_audio_handler',
    'api_post_audio_handler',
    'api_get_buffer_handler',
    'api_post_buffer_handler',
    'api_get_tasks_handler',
    'api_post_tasks_handler',
    'api_get_error_handler',
    'api_post_error_handler',
    'api_get_debug_handler',
    'api_post_debug_handler',
    'api_get_all_config_handler',
    'api_post_save_handler',
    'api_post_load_handler',
]

# Process each handler
for handler in handlers:
    # Find the handler function definition
    pattern = rf'(static esp_err_t {handler}\(httpd_req_t \*req\) \{{\n)'
    
    # Check if auth already added
    check_pattern = rf'{handler}\(httpd_req_t \*req\) \{{\n    // Check authentication'
    if re.search(check_pattern, content):
        print(f"✓ {handler} - already has auth")
        continue
    
    # Add auth check right after function opening brace
    replacement = r'\1' + auth_check
    new_content = re.sub(pattern, replacement, content)
    
    if new_content != content:
        content = new_content
        print(f"+ {handler} - added auth")
    else:
        print(f"? {handler} - NOT FOUND")

# Write the modified content
with open('src/modules/web_server.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print("\n✅ File updated successfully!")
