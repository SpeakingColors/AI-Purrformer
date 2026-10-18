# Engine

Local Python server. Runs in an Ubuntu container with ROCm PyTorch. Skeleton only.

Run (after `pip install -r requirements.txt`):

    uvicorn purrformer.server:app --host 127.0.0.1 --port 8765
