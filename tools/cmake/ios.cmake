set(MTK_BUILD iOS)
set(DEBUG False)
set(PLATFORM OS64COMBINED)
include(${CMAKE_CURRENT_LIST_DIR}/resources/ios.toolchain.cmake)

add_compile_options(-D__MTK_IOS__)
