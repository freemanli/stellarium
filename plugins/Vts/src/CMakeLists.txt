INCLUDE_DIRECTORIES(
    .
    ${CMAKE_BINARY_DIR}/plugins/Vts/src
)

LINK_DIRECTORIES(/src)

SET(Vts_SRCS
    Vts.hpp
    Vts.cpp
)

ADD_LIBRARY(Vts-static STATIC ${Vts_SRCS})
SET_TARGET_PROPERTIES(Vts-static PROPERTIES OUTPUT_NAME "Vts")
TARGET_LINK_LIBRARIES(Vts-static Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)
SET_TARGET_PROPERTIES(Vts-static PROPERTIES COMPILE_FLAGS "-DQT_STATICPLUGIN")
ADD_DEPENDENCIES(AllStaticPlugins Vts-static)

SET_TARGET_PROPERTIES(Vts-static PROPERTIES FOLDER "plugins/Vts")
