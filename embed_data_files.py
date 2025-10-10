"""
PlatformIO build script to embed web UI data files as binary data
This replicates ESP-IDF's target_add_binary_data() functionality
"""
Import("env")
import os

# Define the data files to embed
data_files = [
    "data/index.html",
    "data/config.html",
    "data/monitor.html",
    "data/ota.html",
    "data/logs.html",
    "data/network.html",
    "data/css/style.css",
    "data/js/api.js",
    "data/js/utils.js",
    "data/js/app.js",
    "data/js/config.js",
    "data/js/monitor.js",
    "data/js/ota.js",
    "data/js/logs.js",
    "data/js/network.js"
]

build_dir = env.subst("$BUILD_DIR")
project_dir = env.subst("$PROJECT_DIR")

def generate_assembly_file(source_file):
    """Generate .S assembly file from data file"""
    rel_path = os.path.relpath(source_file, project_dir)
    base_name = os.path.basename(source_file)
    safe_name = base_name.replace(".", "_").replace("-", "_")
    
    # Read source file
    with open(os.path.join(project_dir, source_file), 'rb') as f:
        data = f.read()
    
    # Create assembly file
    asm_file = os.path.join(build_dir, f"{base_name}.S")
    
    with open(asm_file, 'w') as f:
        f.write(f"""
    .section .rodata.embedded
    .global _binary_{safe_name}_start
    .global _binary_{safe_name}_end
    .align 4
_binary_{safe_name}_start:
""")
        # Write data as bytes
        for i, byte in enumerate(data):
            if i % 16 == 0:
                f.write("\n    .byte ")
            f.write(f"0x{byte:02x}")
            if i < len(data) - 1 and (i + 1) % 16 != 0:
                f.write(", ")
        
        f.write(f"""
    .byte 0x00
_binary_{safe_name}_end:
""")
    
    return asm_file

# Generate assembly files for all data files
print("Embedding web UI data files...")
for data_file in data_files:
    source_path = os.path.join(project_dir, data_file)
    if os.path.exists(source_path):
        asm_file = generate_assembly_file(data_file)
        print(f"  Generated: {os.path.basename(asm_file)}")
        # Add to build sources
        env.BuildSources(build_dir, asm_file)
    else:
        print(f"  WARNING: File not found: {data_file}")

print("Data embedding complete.")
