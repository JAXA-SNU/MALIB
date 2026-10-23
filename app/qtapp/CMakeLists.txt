# MALIB Qt GUI apps
# build:  cmake -B build -G Ninja && cmake --build build
cmake_minimum_required(VERSION 3.16)
project(malib_qtapp C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

# ---- MALIB core library (same source set / flags as app/consapp/rtkrcv/gcc/makefile)
set(MALIB_CORE_SOURCES
    ${SRC}/rtksvr.c   ${SRC}/biassnx.c  ${SRC}/ephemeris.c ${SRC}/geoid.c
    ${SRC}/inscmn.c   ${SRC}/ionex.c    ${SRC}/lambda.c    ${SRC}/lclcmn.c
    ${SRC}/mdccssr.c  ${SRC}/mdciono.c  ${SRC}/options.c   ${SRC}/pntpos.c
    ${SRC}/ppp.c      ${SRC}/ppp_ar.c   ${SRC}/preceph.c   ${SRC}/rcvraw.c
    ${SRC}/qzssl6.c   ${SRC}/readfcb.c  ${SRC}/rinex.c     ${SRC}/rtcm.c
    ${SRC}/rtcm2.c    ${SRC}/rtcm3.c    ${SRC}/rtcm3lcl.c  ${SRC}/rtcm3e.c
    ${SRC}/rtkcmn.c   ${SRC}/rtkpos.c   ${SRC}/sbas.c      ${SRC}/solution.c
    ${SRC}/stream.c   ${SRC}/tides.c    ${SRC}/satqual.c
    ${SRC}/rcv/binex.c    ${SRC}/rcv/crescent.c ${SRC}/rcv/javad.c
    ${SRC}/rcv/novatel.c  ${SRC}/rcv/nvs.c      ${SRC}/rcv/rt17.c
    ${SRC}/rcv/septentrio.c ${SRC}/rcv/skytraq.c ${SRC}/rcv/ss2.c
    ${SRC}/rcv/ublox.c
)
add_library(malib_core STATIC ${MALIB_CORE_SOURCES})
target_include_directories(malib_core PUBLIC ${SRC})
# NFREQ etc. change struct sizes -> must be identical for lib and GUI (hence PUBLIC)
target_compile_definitions(malib_core PUBLIC
    TRACE ENAGLO ENAGAL ENAQZS ENACMP ENAIRN NFREQ=10 NEXOBS=5 SVR_REUSEADDR)
target_compile_options(malib_core PRIVATE
    -Wall -O2 -Wno-unused-but-set-variable -Wno-stringop-truncation)
if (NOT WIN32)
    # -mcmodel=large: rtkrcv makefile と同じ (ELF専用、MinGW では使えない)
    target_compile_options(malib_core PRIVATE -mcmodel=large)
    target_link_libraries(malib_core PUBLIC m rt pthread)
else()
    # rtklib.h は WIN32 (アンダースコア無し) を見る。MinGW は自動定義しないので明示
    target_compile_definitions(malib_core PUBLIC WIN32)
    target_link_libraries(malib_core PUBLIC ws2_32 winmm)
endif()

# ---- common dialogs (Qt port of app/winapp/appcmn)
add_library(appcmn_qt STATIC
    appcmn_qt/graph.cpp appcmn_qt/graph.h
    appcmn_qt/tcpoptdlg.cpp appcmn_qt/tcpoptdlg.h
    appcmn_qt/serioptdlg.cpp appcmn_qt/serioptdlg.h
    appcmn_qt/cmdoptdlg.cpp appcmn_qt/cmdoptdlg.h
    appcmn_qt/ftpoptdlg.cpp appcmn_qt/ftpoptdlg.h
    appcmn_qt/refdlg.cpp appcmn_qt/refdlg.h
)
target_include_directories(appcmn_qt PUBLIC appcmn_qt rtknavi_qt)
target_link_libraries(appcmn_qt PUBLIC Qt6::Widgets malib_core)

# ---- Phase 1 spike: open TcpOptDialog standalone
qt_add_executable(spike_tcpopt spike/main.cpp)
target_link_libraries(spike_tcpopt PRIVATE appcmn_qt)

# ---- RTKNAVI-Qt (Phase 2 skeleton: conf-driven, dialogs come in Phase 3)
# WIN32: build as a GUI subsystem app so no console window appears.
# --selftest / --coverage still print: main.cpp attaches to the parent
# console when started from cmd (see AttachConsole there).
qt_add_executable(rtknavi_qt WIN32
    rtknavi_qt/main.cpp rtknavi_qt/navimain.cpp rtknavi_qt/navimain.h
    rtknavi_qt/plotpanel.cpp rtknavi_qt/plotpanel.h
    rtknavi_qt/instrdlg.cpp rtknavi_qt/instrdlg.h
    rtknavi_qt/outstrdlg.cpp rtknavi_qt/outstrdlg.h
    rtknavi_qt/optdlg.cpp rtknavi_qt/optdlg.h
    rtknavi_qt/snrwdlg.cpp rtknavi_qt/snrwdlg.h
    rtknavi_qt/mondlg.cpp rtknavi_qt/mondlg.h
)
target_link_libraries(rtknavi_qt PRIVATE appcmn_qt)
if (NOT WIN32)
    target_compile_options(rtknavi_qt PRIVATE -mcmodel=large)
endif()

# ---- rtknavi_cli : --selftest / --coverage as a console subsystem program.
# rtknavi_qt is a GUI subsystem binary (no console window from Explorer), which
# means a shell neither waits for it nor reliably connects its stdout: under
# PowerShell the redirected file comes out empty and %ERRORLEVEL% is not the
# program's exit code.  This target exists so the checks can be piped and their
# exit code read.  No WIN32 flag here on purpose.
qt_add_executable(rtknavi_cli
    rtknavi_qt/maincli.cpp
    rtknavi_qt/optdlg.cpp rtknavi_qt/optdlg.h
    rtknavi_qt/snrwdlg.cpp rtknavi_qt/snrwdlg.h
)
target_link_libraries(rtknavi_cli PRIVATE appcmn_qt)
if (NOT WIN32)
    target_compile_options(rtknavi_cli PRIVATE -mcmodel=large)
endif()

# ---- RTKPLOT-Qt (solution plotting: track / position / velocity / nsat)
qt_add_executable(rtkplot_qt WIN32
    rtkplot_qt/main.cpp
    rtkplot_qt/plotmain.cpp rtkplot_qt/plotmain.h
    rtkplot_qt/plotwidget.cpp rtkplot_qt/plotwidget.h
    rtkplot_qt/plotdata.cpp rtkplot_qt/plotdata.h
    rtkplot_qt/conndlg.cpp rtkplot_qt/conndlg.h
    rtkplot_qt/plotopt.cpp rtkplot_qt/plotopt.h
)
target_include_directories(rtkplot_qt PRIVATE rtkplot_qt)
target_link_libraries(rtkplot_qt PRIVATE appcmn_qt)
if (NOT WIN32)
    target_compile_options(rtkplot_qt PRIVATE -mcmodel=large)
endif()
