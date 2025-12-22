Import("env")
import os

# Get the framework dir
framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")

# Add Arduino framework library include paths
lib_paths = [
    os.path.join(framework_dir, "libraries", "WiFi", "src"),
    os.path.join(framework_dir, "libraries", "Preferences", "src"),
    os.path.join(framework_dir, "libraries", "Ethernet", "src"),
    os.path.join(framework_dir, "libraries", "WiFiClientSecure", "src"),
]

# Add only to the project's source compilation
env.Append(CPPPATH=lib_paths)
