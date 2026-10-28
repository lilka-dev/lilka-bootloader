# Builds the Lilka boot menu (2nd stage bootloader) and the shared partition table.
# The app in main/ is a placeholder: only `idf.py bootloader partition-table` matters.
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lilka_boot)
