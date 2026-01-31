# ----------------------------
# Makefile Options
# ----------------------------

NAME = SLOTS
ICON = icon.png
DESCRIPTION = "Brainrot Slots"
COMPRESSED = YES
CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------
include $(shell cedev-config --makefile)
