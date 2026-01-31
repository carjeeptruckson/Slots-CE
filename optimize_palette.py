from PIL import Image
import os
import math

def calculate_distance(c1, c2):
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(c1, c2)))

def optimize_image(image_path, threshold=15):
    img = Image.open(image_path)
    img = img.convert("RGBA")
    pixels = img.load()
    width, height = img.size
    
    colors = {}
    for y in range(height):
        for x in range(width):
            color = pixels[x, y]
            if color[3] == 0: continue # Skip transparency
            if color not in colors:
                colors[color] = []
            colors[color].append((x, y))
            
    sorted_colors = sorted(colors.keys(), key=lambda c: sum(c[:3]))
    
    mapping = {}
    
    for i, color in enumerate(sorted_colors):
        if color in mapping: continue
        
        # Check against subsequent colors
        # This is a simple greedy approach
        group = [color]
        
        for other_color in sorted_colors[i+1:]:
            if other_color in mapping: continue
            
            if calculate_distance(color, other_color) < threshold:
                mapping[other_color] = color
                
    # Apply mapping
    count = 0
    for original, target in mapping.items():
        if original in colors:
            for x, y in colors[original]:
                pixels[x, y] = target
            count += 1
            
    if count > 0:
        print(f"Optimized {os.path.basename(image_path)}: Merged {count} colors.")
        img.save(image_path)
    else:
        print(f"No optimization needed for {os.path.basename(image_path)}")

def main():
    directory = "src/gfx"
    if not os.path.exists(directory):
        print(f"Directory {directory} not found.")
        return

    for filename in os.listdir(directory):
        if filename.endswith(".png"):
            filepath = os.path.join(directory, filename)
            try:
                optimize_image(filepath)
            except Exception as e:
                print(f"Failed to process {filename}: {e}")

if __name__ == "__main__":
    main()
