import sys
import os
from pathlib import Path
from PIL import Image

def generate_ico(png_path, ico_path):
    img = Image.open(png_path)
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    
    sizes = [16, 32, 48, 64, 128, 256]
    images = [img.resize((s, s), Image.LANCZOS) for s in sizes]
    img.save(ico_path, format="ICO", append_images=images[1:])
    print(f"ICO generated: {ico_path}")

def main():
    icon_png = sys.argv[1] if len(sys.argv) > 1 else "assets/icon_mimo.png"
    
    if not Path(icon_png).exists():
        print(f"Icon not found: {icon_png}")
        sys.exit(1)
    
    ico_path = "assets/icon.ico"
    generate_ico(icon_png, ico_path)
    
    os.system("pyinstaller mimo-island.spec --clean")
    print("\nDone! Output: dist/MiMo-Island.exe")

if __name__ == "__main__":
    main()
