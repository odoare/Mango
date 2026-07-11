# CMake generated Testfile for 
# Source directory: /home/doare/src/Mango
# Build directory: /home/doare/src/Mango/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[MangoTests]=] "/home/doare/src/Mango/build/MangoTests")
set_tests_properties([=[MangoTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/doare/src/Mango/CMakeLists.txt;114;add_test;/home/doare/src/Mango/CMakeLists.txt;0;")
add_test([=[MangoRenderTest]=] "/home/doare/src/Mango/build/MangoRenderTest_artefacts/Release/MangoRenderTest")
set_tests_properties([=[MangoRenderTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/doare/src/Mango/CMakeLists.txt;151;add_test;/home/doare/src/Mango/CMakeLists.txt;0;")
subdirs("JUCE")
