set(MTK_BUILD Android)
set(DEBUG False)
if (NOT DEFINED MTK_ANDROID_NDK)
    set(MTK_ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
endif()
set(ANDROID_ABI "arm64-v8a")
set(ANDROID_PLATFORM "android-22")
set(ANDROID_STL "c++_static")
include(${MTK_ANDROID_NDK}/build/cmake/android.toolchain.cmake)

add_compile_options(-D__MTK_ANDROID__)
