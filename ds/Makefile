# ChessDS - Nintendo DS client for the ChessDS Lichess bridge.
#
# Build with BlocksDS (https://blocksds.skylyrac.net): run `make` here, or
# ../build.ps1 / ../build.sh to build inside the BlocksDS Docker image.

BLOCKSDS	?= /opt/blocksds/core

NAME		:= chessds
GAME_TITLE	:= ChessDS
GAME_SUBTITLE	:= Lichess over WiFi
GAME_AUTHOR	:= devmchechi
GAME_ICON	:= icon.gif

SOURCEDIRS	:= source

# The ARM7 core must include DSWiFi because the ARM9 code uses it.
ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
LIBS		:= -ldswifi9 -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi

CFLAGS		:= -Wextra -Wno-unused-parameter

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
