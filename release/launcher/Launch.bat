@echo off
rem Nox Modernized: start the game with this file (NoxModernized-Settings.exe sets GAMEDIR to your Nox folder).
rem Game data, saves and nox.cfg come from the GOG install (working directory).
set GAMEDIR=C:\path\to\Nox
cd /d "%GAMEDIR%"
start "" "%~dp0Game.exe"
