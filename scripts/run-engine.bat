@echo off
REM Starts the local engine on 127.0.0.1:8765 (Windows). AGPL-3.0.
REM Needs Python 3.10+ installed (tick "Add python to PATH").
cd /d "%~dp0engine"
if not exist .venv python -m venv .venv
call .venv\Scripts\activate.bat
pip install -q -r requirements.txt
python -m uvicorn purrformer.server:app --host 127.0.0.1 --port 8765
