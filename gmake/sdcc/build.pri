# Bring in the dependencies tree 
include $(ROOT)$(PROJECT_DIR)/dependencies.pri
include $(ROOT)tp_build/gmake/parse_dependencies.pri

DEFINES  := $(foreach DEFINE,$(DEFINES),-D$(DEFINE))
INCLUDES += $(foreach INCLUDE,$(INCLUDEPATHS),-I./$(INCLUDE))

DEFINES += -DTP_SDCC

ARCHIVES = $(addsuffix .lib,$(addprefix $(ROOT)$(BUILD_DIR),$(SUBDIRS)))
HEX = $(TARGET_BUILD_DIR).hex
BIN = $(TARGET_BUILD_DIR).bin

all: $(BIN) $(HEX)

$(HEX): $(BUILD_DIR) $(SUBDIRS) $(ARCHIVES)
	$(CC) $(LDFLAGS) $(ROOT)$(MAIN_SRC) $(ARCHIVES) $(LIBS) $(INCLUDES) $(DEFINES) -o $@

$(BIN): $(HEX)
	$(MAKEBIN) -p $< $@

$(BUILD_DIR):
	$(MKDIR) $(BUILD_DIR)

$(SUBDIRS): force_look
	for d in $@ ; do (cd $$d ; make ) ; done

install:
	-for d in $(SUBDIRS) ; do (cd $$d; $(MAKE) install ); done

clean:
	-for d in $(SUBDIRS); do (cd $$d; $(MAKE) clean ); done

force_look :
	true

