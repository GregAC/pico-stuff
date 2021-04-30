from PIL import Image

def load_spritesheet(spritesheet_image_filename, sprite_width, sprite_height):
    spritesheet_image = Image.open(spritesheet_image_filename)

    if (spritesheet_image.width % sprite_width) != 0:
        print(f'Error spritesheet image {spritesheet_image_filename} must be a multiple of ' \
            f'{sprite_width}')
        return None

    if (spritesheet_image.height % sprite_height) != 0:
        print(f'Error spritesheet image {spritesheet_image_filename} must be a multiple of ' \
            f'{sprite_height}')
        return None

    return {'image' : spritesheet_image, 'sprite_width' : sprite_width,
            'sprite_height' : sprite_height, 'width' : spritesheet_image.width // sprite_width,
            'height' : spritesheet_image.height // sprite_height}

def get_sprite_image(sprite, spritesheet):
    sprite_x = (sprite % spritesheet['width']) * spritesheet['sprite_width']
    sprite_y = (sprite // spritesheet['width']) * spritesheet['sprite_height']

    return spritesheet['image'].crop((sprite_x, sprite_y, sprite_x + spritesheet['sprite_width'],
        sprite_y + spritesheet['sprite_height']))

def rgb_to_rgb555(rgb):
    return (rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10)

def write_sprite_image_to_c_header(sprite_image, c_header_file):
    for y in range(sprite_image.height):
        for x in range(sprite_image.width):
            pix_rgb = sprite_image.getpixel((x, y))[:3]
            pix_rgb555 = rgb_to_rgb555(pix_rgb)
            c_header_file.write(f'  0x{pix_rgb555:04x},\n')

def spritesheet_to_c_header(spritesheet, c_header_filename, name):
    c_header_file = open(c_header_filename, 'w')

    num_sprites = spritesheet['width'] * spritesheet['height']
    c_header_file.write(f'int num_{name} = {num_sprites};\n')
    c_header_file.write(f'int {name}_height = {spritesheet["sprite_height"]};\n\n')
    c_header_file.write(f'uint16_t {name}_data[] = {{\n')

    for sprite in range(num_sprites):
        sprite_image = get_sprite_image(sprite, spritesheet)
        write_sprite_image_to_c_header(sprite_image, c_header_file)

    c_header_file.write('};')
    c_header_file.close()

# Read in 16x18 sprites from 'char_sheet.png' and write them out to
# 'sprite_data.h' for direct use in a C program
test_spritesheet = load_spritesheet('char_sheet.png', 16, 18)
spritesheet_to_c_header(test_spritesheet, 'sprite_data.h', 'sprite')
