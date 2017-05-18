PROJECT_NAME=Vulkan01
TARGET=target01

PROJECT_OUTPUT_DIR = ../../bin/linux-aarch64/

ifeq ($(GNUTOOL_PATH),)
GNUTOOL_PATH = 
endif

ifeq ($(GNUTOOL_PREFIX),)
GNUTOOL_PREFIX=aarch64-linux-gnu-
endif

SOURCE_PATH 	= ./../../source/
EXTERNAL_PATH 	= ./../../externals
EXTERNAL_TARGET_PATH = $(EXTERNAL_PATH)/$(TARGET)

C =  $(GNUTOOL_PATH)$(GNUTOOL_PREFIX)gcc
CC = $(GNUTOOL_PATH)$(GNUTOOL_PREFIX)g++
AR = $(GNUTOOL_PATH)$(GNUTOOL_PREFIX)as

CFLAGS=-c -Wall -DLINUX -D_LINUX 

INCLUDE_PATHS = \
					-I$(EXTERNAL_PATH)/include		\
					-I$(EXTERNAL_TARGET_PATH)/include
					
LIB_PATHS = \
					-Wl,--library-path=$(EXTERNAL_PATH)/libs,-rpath-link=$(EXTERNAL_PATH)/libs \
					-Wl,--library-path=$(EXTERNAL_TARGET_PATH)/libs,-rpath-link=$(EXTERNAL_TARGET_PATH)/libs \
								

EXTRA_FLAGS = 	-I$(SOURCE_PATH) $(INCLUDE_PATHS)
	      
LIBS = -lvulkan
		  
LDFLAGS= $(LIBS) $(LIB_PATHS)

C_SOURCES=	

CPP_SOURCES= \
			$(SOURCE_PATH)common.cpp	\
			$(SOURCE_PATH)vulkan_base.cpp	\
			
		

CPP_OBJECTS=$(CPP_SOURCES:.cpp=.o)

C_OBJECTS=$(C_SOURCES:.c=.o)

EXECUTABLE=$(PROJECT_OUTPUT_DIR)$(PROJECT_NAME)

.PHONY: all

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(CPP_OBJECTS) $(C_OBJECTS) 
	$(CC) $(LDFLAGS) $(CPP_OBJECTS) $(C_OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $(EXTRA_FLAGS) $< -o $@
	
.c.o:
	$(C) $(CFLAGS) $(EXTRA_FLAGS) $< -o $@	

.PHONY: clean
clean:
	rm -f $(EXECUTABLE) $(OBJECTS)
	rm $(SOURCE_PATH)/*.o

	

