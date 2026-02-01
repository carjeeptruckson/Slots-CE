# ----------------------------
# Makefile Options
# ----------------------------

NAME = SLOTS
ICON = icon.png
DESCRIPTION = "Slot Machine Game"
COMPRESSED = YES
CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------
include $(shell cedev-config --makefile)
