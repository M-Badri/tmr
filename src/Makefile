# ============================================
#
# Make file for TMR_DIR/src/
#
# ============================================

include ../Makefile.in
include ../TMR_Common.mk

CXX_OBJS = TMROctant.o \
	TMROctree.o

DIR=${TMR_DIR}/src

CXX_OBJS := $(CXX_OBJS:%=$(DIR)/%)

default: ${CXX_OBJS}
	${AR} ${AR_FLAGS} ${TMR_LIB} ${CXX_OBJS}

debug: TMR_CC_FLAGS=${TMR_DEBUG_CC_FLAGS}
debug: default

clean:
	${RM} ${CXX_OBJS}