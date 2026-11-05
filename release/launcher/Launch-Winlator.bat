@echo off
rem Nox Modernized for Winlator: start the game with this file. NoxModernized-Settings.exe (started in Winlator) sets GAMEDIR to your Nox folder.
set GAMEDIR=D:\path\to\Nox
rem Winlator's own settings, lighter for phones and tablets: nox-modernized-winlator.ini
set ONC_INI=nox-modernized-winlator.ini
cd /d "%GAMEDIR%"
start "" "%~dp0Game.exe"
