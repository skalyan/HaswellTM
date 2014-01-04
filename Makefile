CXX=g++
CC=gcc

CPPFLAGS_BASE=-lpthread -std=gnu++0x -O0
CPPFLAGS_RTM=-DUSE_RTM
CPPFLAGS_HLE=-DHLE_ON
CPPFLAGS_NLK=-DNO_LOCKING
CPPFLAGS_DBG=-g
CPPFLAGS=${CPPFLAGS_BASE} ${CPPFLAGS_DBG}

CFLAGS=
HEADER_FILES = FallbackLock.hpp RHash.hpp TransRegion.hpp

clean:
	rm -f exe.*

OBJFILES = hLock.o

hLock.o : hLock.c
	${CC} ${CFLAGS} -DTURN_ON_HLE -c hLock.c

RTMHashDriver: rtmHashDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_RTM} ${OBJFILES} rtmHashDriver.cpp -o exe.rtmHashDriver

HLEHashDriver: rtmHashDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_HLE} ${OBJFILES} rtmHashDriver.cpp -o exe.HLEHashDriver

CLHashDriver: rtmHashDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${OBJFILES} rtmHashDriver.cpp -o exe.CLHashDriver

NLKHashDriver: rtmHashDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_NLK} rtmHashDriver.cpp -o exe.NLKHashDriver

RTMSimpleDriver: rtmSimpleDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_RTM} ${OBJFILES} rtmSimpleDriver.cpp -o exe.rtmSimpleDriver

HLESimpleDriver: rtmSimpleDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_HLE} ${OBJFILES} rtmSimpleDriver.cpp -o exe.HLESimpleDriver

CLSimpleDriver: rtmSimpleDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${OBJFILES} rtmSimpleDriver.cpp -o exe.CLSimpleDriver

NLKSimpleDriver: rtmSimpleDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_NLK} rtmSimpleDriver.cpp -o exe.NLKSimpleDriver

RTMSSDriver: rtmSingleSlotDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_RTM} ${OBJFILES} rtmSingleSlotDriver.cpp -o exe.rtmSSDriver

HLESSDriver: rtmSingleSlotDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${CPPFLAGS_HLE} ${OBJFILES} rtmSingleSlotDriver.cpp -o exe.hleSSDriver

SpinHashDriver: rtmHashDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${OBJFILES} rtmHashDriver.cpp -o exe.spinHashDriver

SpinSSDriver: rtmSingleSlotDriver.cpp ${HEADER_FILES} ${OBJFILES}
	${CXX} ${CPPFLAGS} ${OBJFILES} rtmSingleSlotDriver.cpp -o exe.spinSSDriver

all: clean RTMHashDriver RTMSSDriver SpinHashDriver SpinSSDriver CLHashDriver HLEHashDriver NLKHashDriver \
    RTMSimpleDriver CLSimpleDriver HLESimpleDriver NLKSimpleDriver HLESSDriver
