
##Use:
##TP_RC += file.qrc

TP_RC_TOOL_SOURCE = $$absolute_path(../tp_rc/tp_rc.cpp)
TP_RC_TOOL = $$absolute_path($$OUT_PWD/../tp_rc/tpRc)

buildtprc.input = TP_RC_TOOL_SOURCE
buildtprc.output = $${TP_RC_TOOL}
buildtprc.target = buildtprc
buildtprc.commands = g++ -std=gnu++1z -O2 $$TP_RC_TOOL_SOURCE -o $$TP_RC_TOOL
buildtprc.CONFIG += no_link
QMAKE_EXTRA_COMPILERS += buildtprc

tpRc.name = Compiling resources using tpRc
tpRc.input = TP_RC
tpRc.output = $${OUT_PWD}/${QMAKE_FILE_BASE}.cpp
tpRc.commands = $${TP_RC_TOOL} ${QMAKE_FILE_IN} $${OUT_PWD}/${QMAKE_FILE_BASE}.cpp
tpRc.variable_out = SOURCES
tpRc.depends = $${TP_RC_TOOL}
QMAKE_EXTRA_COMPILERS += tpRc
