# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/v5.2.1/esp-idf/components/bootloader/subproject"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/tmp"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/src"
  "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Proyectos_ESP/Pichardo/MQTT_1/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
