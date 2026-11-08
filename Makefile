# Makefile for MinGW-w64

CONFIG ?= Release
BUILD_DIR ?= build/$(CONFIG)
WIL_INCLUDE ?= packages/Microsoft.Windows.ImplementationLibrary.1.0.260126.7/include

CXX     ?= g++
WINDRES ?= windres

TARGET := $(BUILD_DIR)/MinimalImageViewer.exe
SOURCES := \
	src/exif_utils.cpp \
	src/image_drawing.cpp \
	src/image_edit.cpp \
	src/image_io.cpp \
	src/main.cpp \
	src/settings_handler.cpp \
	src/ui_actions.cpp \
	src/ui_dialogs.cpp \
	src/ui_handlers.cpp \
	src/ui_properties.cpp \
	src/ui_tools.cpp
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
RESOURCE := $(BUILD_DIR)/Resource.o
DEPS := $(OBJECTS:.o=.d)

DEFINES := \
        -DUNICODE -D_UNICODE \
        -D_WINDOWS -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00

PROJECT_CXXFLAGS := -m64 -std=c++20 -finput-charset=UTF-8 -fexec-charset=UTF-8 \
	-Wall -Wextra -Wno-unknown-pragmas

PROJECT_LDFLAGS := -m64 -municode -mwindows -static -static-libgcc -static-libstdc++

ifeq ($(CONFIG),Debug)
  DEFINES += -D_DEBUG -DRESULT_NO_DEBUG
  PROJECT_CXXFLAGS += -O0 -g
else ifeq ($(CONFIG),Release)
  DEFINES += -DNDEBUG
  PROJECT_CXXFLAGS += -O2 -g -flto -ffunction-sections -fdata-sections
  PROJECT_LDFLAGS += -flto -Wl,--gc-sections
else
  $(error CONFIG must be Debug or Release)
endif

# Includes both vcxproj dependencies and libraries from #pragma comment(lib).
PROJECT_LIBS := \
        -luser32 -lgdi32 -lcomdlg32 -lshlwapi -lwindowscodecs \
	-lole32 -lshell32 -lpropsys -loleaut32 -ladvapi32 -ld2d1 -ldwrite \
	-lcomctl32 -ldwmapi -ld3d11 -ldxgi -ldxguid -lwinmm -luuid

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) $(RESOURCE)
	$(CXX) $(PROJECT_LDFLAGS) $(LDFLAGS) -o "$@" $(OBJECTS) $(RESOURCE) $(PROJECT_LIBS) $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.cpp Makefile | $(BUILD_DIR)
	$(CXX) $(DEFINES) -Isrc -I"$(WIL_INCLUDE)" $(CPPFLAGS) $(PROJECT_CXXFLAGS) $(CXXFLAGS) -MMD -MP -c "$<" -o "$@"

$(RESOURCE): src/Resource.rc src/Resource.h src/app.ico Makefile | $(BUILD_DIR)
	$(WINDRES) --target=pe-x86-64 --codepage=65001 $(DEFINES) -Isrc $(RCFLAGS) -i "$<" -o "$@" -O coff

$(BUILD_DIR):
	mkdir -p "$@"

# Removes only the selected configuration's output directory.
clean:
	rm -rf "$(BUILD_DIR)"

-include $(DEPS)
