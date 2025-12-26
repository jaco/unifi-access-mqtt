Import("env")
import gzip
import os
import shutil
import subprocess
from datetime import datetime

def get_ui_build_id():
    """Generate a build ID from git hash and timestamp"""
    project_dir = env.subst("$PROJECT_DIR")

    # Get git short hash
    git_hash = "unknown"
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, cwd=project_dir
        )
        if result.returncode == 0:
            git_hash = result.stdout.strip()
    except Exception:
        pass

    # Check for dirty state
    dirty = ""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True, text=True, cwd=project_dir
        )
        if result.returncode == 0 and result.stdout.strip():
            dirty = "-dirty"
    except Exception:
        pass

    # Add timestamp
    timestamp = datetime.now().strftime("%m%d-%H%M")

    return f"{git_hash}{dirty}-{timestamp}"

def gzip_webfiles(source, target, env):
    """Gzip web files before building the filesystem image."""
    data_dir = os.path.join(env.subst("$PROJECT_DIR"), "data")

    # Generate build ID
    build_id = get_ui_build_id()
    print(f"  UI Build ID: {build_id}")

    # Files to gzip
    extensions = ['.html', '.js', '.css']

    for filename in os.listdir(data_dir):
        filepath = os.path.join(data_dir, filename)

        # Skip if not a file or already gzipped
        if not os.path.isfile(filepath):
            continue
        if filename.endswith('.gz'):
            continue

        # Check if it's a web file we want to gzip
        _, ext = os.path.splitext(filename)
        if ext not in extensions:
            continue

        gz_path = filepath + '.gz'

        # Always rebuild if source has __UI_BUILD_ID__ placeholder
        force_rebuild = False
        if filename == 'app.js':
            with open(filepath, 'r') as f:
                if '__UI_BUILD_ID__' in f.read():
                    force_rebuild = True

        # Check if gzip is up to date
        if not force_rebuild and os.path.exists(gz_path):
            if os.path.getmtime(gz_path) >= os.path.getmtime(filepath):
                print(f"  Skipping {filename} (up to date)")
                continue

        # Read and process file content
        print(f"  Gzipping {filename}")
        with open(filepath, 'rb') as f_in:
            content = f_in.read()

        # Replace build ID placeholder in JS files
        if filename == 'app.js':
            content = content.replace(b'__UI_BUILD_ID__', build_id.encode('utf-8'))

        # Gzip the content
        with gzip.open(gz_path, 'wb', compresslevel=9) as f_out:
            f_out.write(content)

        # Show size reduction
        orig_size = len(content)
        gz_size = os.path.getsize(gz_path)
        reduction = (1 - gz_size / orig_size) * 100
        print(f"    {orig_size} -> {gz_size} bytes ({reduction:.1f}% reduction)")

# Run before building filesystem
env.AddPreAction("$BUILD_DIR/littlefs.bin", gzip_webfiles)
