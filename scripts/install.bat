@echo off
REM Offline installer (Windows). Run from the unzipped bundle. Needs Python 3.10+ already installed. AGPL-3.0.
cd /d "%~dp0"
python -m venv engine\.venv || goto :fail
call engine\.venv\Scripts\activate.bat
pip install --no-index --find-links wheelhouse -r engine\requirements.txt -r engine\requirements-runtime.txt torch torchaudio || goto :fail
xcopy /E /I /Y "AI Purrformer.vst3" "%CommonProgramFiles%\VST3\AI Purrformer.vst3" || goto :fail
echo @echo off> start-engine.bat
echo cd /d "%%~dp0engine">> start-engine.bat
echo call .venv\Scripts\activate.bat>> start-engine.bat
echo python -m uvicorn purrformer.server:app --host 127.0.0.1 --port 8765>> start-engine.bat
echo Installed. Double click start-engine.bat (no internet needed), then rescan plugins in your DAW.
pause
exit /b 0
:fail
echo Install failed, see the messages above.
pause
exit /b 1
