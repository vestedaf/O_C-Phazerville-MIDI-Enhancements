Import("env")

# GCC 14+ toolchain makes reinterpret_cast from integer to pointer a
# hard error. The Teensy framework's imxrt.h uses integer-to-pointer
# casts for hardware register access. This flag downgrades that error
# back to a warning.
env.Append(CCFLAGS=["-fpermissive"])