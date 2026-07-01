"""Post-install patch: apply PSRAM fix to old framework cores."""
Import("env")
import os, subprocess

platform = env.PioPlatform()
cores_dir = os.path.join(
    platform.get_package_dir("framework-arduinoteensy") or "",
    "cores"
)

if os.path.isdir(cores_dir):
    startup_file = os.path.join(cores_dir, "teensy4", "startup.c")
    if os.path.isfile(startup_file):
        with open(startup_file) as f:
            content = f.read()
        old = "uint8_t size2 = flexspi2_psram_size(size1 << 20);"
        new = "uint8_t size2 = 0; // flexspi2_psram_size(size1 << 20); // disable 2nd PSRAM"
        if old in content:
            content = content.replace(old, new)
            with open(startup_file, 'w') as f:
                f.write(content)
            print("PSRAM fix applied to startup.c")
        elif new in content:
            print("PSRAM fix already applied")
        else:
            print("WARN: Could not find PSRAM probe line")
    else:
        print(f"WARN: startup.c not found at {startup_file}")
else:
    print(f"WARN: cores dir not found at {cores_dir}")