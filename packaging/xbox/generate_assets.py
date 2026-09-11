import os
from PIL import Image, ImageDraw, ImageFont

os.makedirs("Assets", exist_ok=True)

def create_logo(width, height, filename, text="N"):
    # Dark modern Xbox/Switch red/slate background
    img = Image.new("RGBA", (width, height), color=(24, 25, 28, 255))
    draw = ImageDraw.Draw(img)
    
    # Accent rounded box (Switch Neon Red #E60012)
    padding = max(4, width // 10)
    draw.rounded_rectangle(
        [(padding, padding), (width - padding, height - padding)],
        radius=max(2, width // 8),
        fill=(230, 0, 18, 255)
    )
    
    # Inner white circle/emblem
    center_x, center_y = width // 2, height // 2
    radius = max(2, width // 4)
    draw.ellipse(
        [(center_x - radius, center_y - radius), (center_x + radius, center_y + radius)],
        fill=(255, 255, 255, 255)
    )
    
    img.save(f"Assets/{filename}", "PNG")
    print(f"Created Assets/{filename} ({width}x{height})")

create_logo(150, 150, "Square150x150Logo.png", "N")
create_logo(44, 44, "Square44x44Logo.png", "N")
create_logo(50, 50, "StoreLogo.png", "N")
create_logo(620, 300, "SplashScreen.png", "NEMU")
