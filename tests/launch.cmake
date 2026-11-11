# SPDX-License-Identifier: GPL-2.0-or-later
# Copies the app to an empty directory, starts it with a scratch config and
# expects a screenshot: proof that the window and its QML came up.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/bin" "${WORK}/config")
file(COPY "${APP}" DESTINATION "${WORK}/bin")
get_filename_component(name "${APP}" NAME)
set(ENV{XDG_CONFIG_HOME} "${WORK}/config")
execute_process(
  COMMAND "${WORK}/bin/${name}" --size 360x880 --screenshot "${WORK}/shot.png" --delay 1500 -e sleep 10
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE out
  TIMEOUT 30)
if(NOT rc EQUAL 0 OR NOT EXISTS "${WORK}/shot.png")
  message(FATAL_ERROR "the app did not come up (exit ${rc}):\n${out}")
endif()
