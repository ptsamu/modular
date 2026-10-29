from PIL import Image, ImageDraw, ImageFilter
import sys

logo_path, output_path = sys.argv[1], sys.argv[2]
size = 1024
canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
rect = (58, 58, 966, 966)
radius = 218

shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
shadow_mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(shadow_mask).rounded_rectangle((58, 76, 966, 984), radius=radius, fill=72)
shadow.putalpha(shadow_mask.filter(ImageFilter.GaussianBlur(28)))
canvas.alpha_composite(shadow)

background = Image.new("RGBA", (size, size), (0, 0, 0, 0))
background_mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(background_mask).rounded_rectangle(rect, radius=radius, fill=255)
pixels = background.load()
for y in range(size):
    blend = max(0.0, min(1.0, (y - 58) / 908))
    r = int(255 * (1 - blend) + 235 * blend)
    g = int(132 * (1 - blend) + 108 * blend)
    b = int(61 * (1 - blend) + 42 * blend)
    for x in range(58, 967):
        pixels[x, y] = (r, g, b, 255)
background.putalpha(background_mask)
canvas.alpha_composite(background)

highlight = Image.new("RGBA", (size, size), (255, 255, 255, 0))
highlight_mask = Image.new("L", (size, size), 0)
ImageDraw.Draw(highlight_mask).rounded_rectangle((74, 74, 950, 520), radius=195, fill=28)
highlight.putalpha(highlight_mask.filter(ImageFilter.GaussianBlur(16)))
canvas.alpha_composite(highlight)

logo = Image.open(logo_path).convert("RGBA")
logo_mask = logo.getchannel("A").resize((528, 528), Image.Resampling.LANCZOS)
mark = Image.new("RGBA", (528, 528), (241, 240, 236, 255))
mark.putalpha(logo_mask)
canvas.alpha_composite(mark, (248, 248))
canvas.save(output_path, "PNG")
