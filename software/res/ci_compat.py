Import("env")

# Force C++14 standard to avoid GCC 14+ hard error on
# reinterpret_cast from integer to pointer in Teensy
# framework headers (imxrt.h). The platform's own
# -std=gnu++17 is set AFTER build_flags are parsed, so
# we must override it here in the pre-script instead.
env.Replace(CXXFLAGS=[x for x in env.get("CXXFLAGS", []) if "-std=" not in x] + ["-std=gnu++14"])