
##Use:
##TP_RC += file.qrc

TP_RC_TOOL_SOURCE = $$absolute_path(../tp_rc/tp_rc.cpp)
TP_RC_TOOL = $$absolute_path($$OUT_PWD/../tpRc)

# tpRc is now built by tp_build/tp_build.pro this means it gets built once and does not trigger a
# relink on every build of any module that has a .qrc file.

tpRc.name = Compiling resources using tpRc
tpRc.input = TP_RC
tpRc.output = $${OUT_PWD}/${QMAKE_FILE_BASE}.cpp
tpRc.commands = $${TP_RC_TOOL} ${QMAKE_FILE_IN} $${OUT_PWD}/${QMAKE_FILE_BASE}.cpp ${QMAKE_FILE_BASE}
tpRc.variable_out = SOURCES

tpRc.depend_command = grep -hs ^ $${OUT_PWD}/${QMAKE_FILE_BASE}.cpp.dep
tpRc.CONFIG = dep_lines

QMAKE_EXTRA_COMPILERS += tpRc
