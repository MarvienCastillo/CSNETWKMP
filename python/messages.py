import json

def serialize_message(msg_dict):
    # Convert dictionary to newline-delimited key:value pairs
    lines = []
    for k, v in msg_dict.items():
        if isinstance(v, dict):
            v = json.dumps(v)
        lines.append(f"{k}: {v}")
    return "\n".join(lines).encode('utf-8')

def deserialize_message(msg_bytes):
    msg = {}
    for line in msg_bytes.decode('utf-8').splitlines():
        if not line.strip():
            continue
        k, v = line.split(': ', 1)
        try:
            v_parsed = json.loads(v)
            msg[k] = v_parsed
        except:
            msg[k] = v
    return msg
