import os
from dotenv import load_dotenv

load_dotenv()

BOT_TOKEN = os.getenv("BOT_TOKEN")
OPENAI_API_KEY = os.getenv("OPENAI_API_KEY")

if not BOT_TOKEN:
    raise ValueError("BOT_TOKEN не найден. Проверь файл .env")
if not OPENAI_API_KEY:
    raise ValueError("OPENAI_API_KEY не найден. Проверь файл .env")