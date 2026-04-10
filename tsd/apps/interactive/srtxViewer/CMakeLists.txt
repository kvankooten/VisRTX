## Copyright 2026 NVIDIA Corporation
## SPDX-License-Identifier: Apache-2.0

project(tsdSrtxViewer)
project_add_executable(
  SrtxViewport.cpp
  tsdSrtxViewer.cpp
)
project_link_libraries(PUBLIC tsd_ui_imgui)


install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
