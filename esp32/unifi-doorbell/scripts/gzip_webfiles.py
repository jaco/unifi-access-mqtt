Import("env")
import gzip
import os
import shutil

def gzip_webfiles(source, target, env):
    """Gzip web files before building the filesystem image."""
    data_dir = os.path.join(env.subst("$PROJECT_DIR"), "data")

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

        # Check if gzip is up to date
        if os.path.exists(gz_path):
            if os.path.getmtime(gz_path) >= os.path.getmtime(filepath):
                print(f"  Skipping {filename} (up to date)")
                continue

        # Gzip the file
        print(f"  Gzipping {filename}")
        with open(filepath, 'rb') as f_in:
            with gzip.open(gz_path, 'wb', compresslevel=9) as f_out:
                shutil.copyfileobj(f_in, f_out)

        # Show size reduction
        orig_size = os.path.getsize(filepath)
        gz_size = os.path.getsize(gz_path)
        reduction = (1 - gz_size / orig_size) * 100
        print(f"    {orig_size} -> {gz_size} bytes ({reduction:.1f}% reduction)")

# Run before building filesystem
env.AddPreAction("$BUILD_DIR/littlefs.bin", gzip_webfiles)
