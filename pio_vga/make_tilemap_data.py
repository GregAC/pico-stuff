import csv
from PIL import Image
from PIL import ImageShow

TILE_WIDTH = 16
TILE_HEIGHT = 16

def load_tilemap_csv(csv_filename):
    with open(csv_filename) as csv_file:
        csv_reader = csv.reader(csv_file)
        rows = list(csv_reader)
        map_width = len(rows[0])
        map_height = len(rows)
        tile_nums = []

        for row_num, row in enumerate(rows):
            if len(row) != map_width:
                print(f'Error row {row_num} is wrong width {map_width} tiles expected')
                return None

            for tile_num_str in row:
                if not tile_num_str.strip().isdigit():
                    printf(f'Error malformed tile number {file_num_str} on row {row_num}')
                    return None

                tile_nums.append(int(tile_num_str))

        return {'width'  : map_width,
                'height' : map_height,
                'tiles'  : tile_nums}

def load_tileset(image_filename):
    tileset_image = Image.open(image_filename)

    if ((tileset_image.width % TILE_WIDTH) != 0):
        print(f'Error tileset image {image_filename} width must be a multiple of {TILE_WIDTH}')
        return None

    if ((tileset_image.height % TILE_HEIGHT) != 0):
        print(f'Error tileset image {image_filename} height must be a multiple of {TILE_HEIGT}')
        return None

    return {'width' : tileset_image.width // TILE_WIDTH,
            'height' : tileset_image.height // TILE_HEIGHT,
            'image' : tileset_image}

def get_tile_image(tile, tileset):
    tileset_x = (tile % tileset['width']) * TILE_WIDTH
    tileset_y = (tile // tileset['width']) * TILE_HEIGHT

    return tileset['image'].crop((tileset_x, tileset_y, tileset_x + TILE_WIDTH,
        tileset_y + TILE_HEIGHT))

def render_tilemap(tilemap, tileset):
    map_image_width = tilemap['width'] * TILE_WIDTH
    map_image_height = tilemap['height'] * TILE_HEIGHT

    map_image = Image.new('RGB', (map_image_width, map_image_height), (255, 0, 255))

    for tile_y in range(tilemap['height']):
        for tile_x in range(tilemap['width']):
            tile = tilemap['tiles'][tile_y * tilemap['width'] + tile_x]
            map_image.paste(get_tile_image(tile, tileset), (tile_x * TILE_WIDTH,
                tile_y * TILE_HEIGHT))

    return map_image

def rgb_to_rgb555(rgb):
    return (rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10)

def write_tile_image_to_c_header(tile_image, c_header_file):
    for y in range(TILE_HEIGHT):
        for x in range(TILE_WIDTH):
            pix_rgb = tile_image.getpixel((x, y))[:3]
            pix_rgb555 = rgb_to_rgb555(pix_rgb)
            c_header_file.write(f'  0x{pix_rgb555:04x},\n')

def tileset_to_c_header(tileset, c_header_filename):
    c_header_file = open(c_header_filename, 'w')

    num_tiles = tileset['width'] * tileset['height']
    c_header_file.write(f'int num_tiles = {num_tiles};\n\n')
    c_header_file.write('uint16_t tileset[] = {\n')

    for tile in range(num_tiles):
        tile_image = get_tile_image(tile, tileset)
        write_tile_image_to_c_header(tile_image, c_header_file)

    c_header_file.write('};')
    c_header_file.close()

def tilemap_to_c_header(tilemap, c_header_filename):
    c_header_file = open(c_header_filename, 'w')

    c_header_file.write(f"int tilemap_width = {tilemap['width']};\n")
    c_header_file.write(f"int tilemap_height = {tilemap['height']};\n")
    c_header_file.write("uint8_t tilemap_tiles = {\n")

    for tile in tilemap['tiles']:
        c_header_file.write(f"  {tile},\n")

    c_header_file.write("};")

# Load tilemap from 'test_map.csv' and tileset from 'village-tiles.png'. Write
# the data from both out to 'test_tilemap.h' and 'village_tileset.h'
# respectively for direct using in a C program. A map preview is drawn to
# 'map-render.png'
test_tilemap = load_tilemap_csv('test_map.csv')
village_tileset = load_tileset('village_tiles.png')
map_image = render_tilemap(test_tilemap, village_tileset)
print(f"{test_tilemap['width']} x {test_tilemap['height']}")
tileset_to_c_header(village_tileset, 'village_tileset.h')
tilemap_to_c_header(test_tilemap, 'test_tilemap.h')
map_image.save('map_render.png')
