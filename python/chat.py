import base64
import os
import time

from messages import serialize_message

def create_text_message(sender, text, seq):
    return serialize_message({
        "message_type": "CHAT_MESSAGE",
        "sender_name": sender,
        "content_type": "TEXT",
        "message_text": text,
        "sequence_number": seq
    })

def create_sticker_message(sender, sticker_path, seq):
    with open(sticker_path, "rb") as f:
        data = f.read()
    encoded = base64.b64encode(data).decode('utf-8')
    return serialize_message({
        "message_type": "CHAT_MESSAGE",
        "sender_name": sender,
        "content_type": "STICKER",
        "sticker_data": encoded,
        "sequence_number": seq
    })

def create_text_message(sender_name, text, sequence_number):
    return {
        "message_type": "CHAT_MESSAGE",
        "sender_name": sender_name,
        "content_type": "TEXT",
        "message_text": text,
        "sequence_number": sequence_number
    }

def create_sticker_message(sender_name, file_path, sequence_number):
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"Sticker file not found: {file_path}")
    
    with open(file_path, "rb") as f:
        data = f.read()
    
    if len(data) > 10 * 1024 * 1024:  # 10 MB limit
        raise ValueError("Sticker file too large (>10MB)")
    
    encoded = base64.b64encode(data).decode('utf-8')
    
    return {
        "message_type": "CHAT_MESSAGE",
        "sender_name": sender_name,
        "content_type": "STICKER",
        "sticker_data": encoded,
        "sequence_number": sequence_number
    }

def save_sticker_message(msg, save_dir="stickers"):
    """
    Saves Base64 sticker data to a PNG file.
    File is named with timestamp to avoid conflicts.
    """
    if not os.path.exists(save_dir):
        os.makedirs(save_dir)
    
    data = base64.b64decode(msg["sticker_data"])
    timestamp = int(time.time() * 1000)
    filename = f"{msg['sender_name']}_{timestamp}.png"
    path = os.path.join(save_dir, filename)
    
    with open(path, "wb") as f:
        f.write(data)
    
    return path
