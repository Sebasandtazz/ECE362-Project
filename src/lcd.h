#define PI 3.14159265358979323846
// screen dimensions 
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// RGB565 color format helper
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))
#define BGR565(r, g, b) (((b & 0xF8) << 8) | ((g & 0xFC) << 3) | ((r & 0xF8) >> 3)) 
#define FONT_SCALE 2  // Scale factor to make text bigger (2x = double size)
uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;  // 4px spacing between lines

// Set the drawing window on the display
// Defines the rectangular area where pixels will be written
// Parameters: (x0, y0) = top-left corner, (x1, y1) = bottom-right corner
void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    send_spi_cmd(spi0, 0x2A); // Column address set command
    send_spi_data16(spi0, x0, true); // Start column
    send_spi_data16(spi0, x1, true); // End column

    send_spi_cmd(spi0, 0x2B); // Row address set command
    send_spi_data16(spi0, y0, true); // Start row
    send_spi_data16(spi0, y1, true); // End row

    send_spi_cmd(spi0, 0x2C); // Memory write command (ready to receive pixel data)
}

// Fill the entire screen with a solid color
// This clears the screen and sets it to the specified background color
void tft_fill_screen(uint16_t color) {
    // Set window to cover entire screen
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    
    // Fill all pixels with the specified color
    // Total pixels = width * height
    uint32_t total_pixels = TFT_WIDTH * TFT_HEIGHT;
    for (uint32_t i = 0; i < total_pixels; i++) {
        send_spi_data16(spi0, color, true);
    }
}

//////////////////////////////////////////////////////////////////////////////

#define FONT_SCALE 2  // Scale factor to make text bigger (2x = double size)

// Draw a single pixel (helper function)
// Sets the display window to a single pixel and writes the color
void tft_draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    tft_set_window(x, y, x, y);
    send_spi_data16(spi0, color, true);
}

// Draw a scaled pixel block (makes text bigger)
// Draws a FONT_SCALE x FONT_SCALE block of pixels at position (x, y)
void tft_draw_scaled_pixel(uint16_t x, uint16_t y, uint16_t color) {
    for (uint8_t i = 0; i < FONT_SCALE; i++) {
        for (uint8_t j = 0; j < FONT_SCALE; j++) {
            tft_draw_pixel(x + i, y + j, color);
        }
    }
}

// Draw a single character at position (x, y) with scaling
// Reads the font bitmap and draws each pixel scaled up
void tft_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color) {
    const unsigned char* char_data = get_char_data(c);
    
    // Draw each row of the font
    for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
        unsigned char row_data = char_data[row];
        // Draw each column (bit) in the row
        for (uint8_t col = 0; col < FONT_WIDTH; col++) {
            // Check if bit is set (MSB first, so bit 7 is leftmost)
            // 0x80 >> col extracts each bit from left to right
            if (row_data & (0x80 >> col)) {
                // Draw foreground color (scaled)
                tft_draw_scaled_pixel(x + (col * FONT_SCALE), y + (row * FONT_SCALE), color);
            } else {
                // Draw background color (scaled)
                tft_draw_scaled_pixel(x + (col * FONT_SCALE), y + (row * FONT_SCALE), bg_color);
            }
        }
    }
}

// Print a string starting at position (x, y)
// Iterates through each character and draws it with proper spacing
void tft_print_string(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg_color) {
    uint16_t x_pos = x;
    // Draw each character in the string
    while (*str) {
        tft_draw_char(x_pos, y, *str++, color, bg_color);
        // Move to next character position (scaled width + spacing)
        x_pos += (FONT_WIDTH * FONT_SCALE) + 1;
    }
}

// Print a string with automatic line wrapping at word boundaries
// Breaks text into multiple lines, wrapping at spaces/punctuation instead of mid-word
// Parameters: start_x, start_y = starting position, line_height = spacing between lines
void tft_print_multiline(uint16_t start_x, uint16_t start_y, const char* str, 
                         uint16_t color, uint16_t bg_color, uint16_t line_height) {
    uint16_t x = start_x;
    uint16_t y = start_y;
    uint16_t char_width = (FONT_WIDTH * FONT_SCALE) + 1;  // Width of one character
    uint16_t max_chars_per_line = (TFT_WIDTH - start_x - 10) / char_width;  // Max chars per line (with margin)
    
    const char* word_start = str;  // Start of current word
    uint16_t word_length = 0;      // Length of current word
    uint16_t line_chars = 0;       // Characters on current line
    
    while (*str) {
        // Check if current character is a space, comma, or period (word boundary)
        if (*str == ' ' || *str == ',' || *str == '.') {
            // Check if the word + space fits on current line
            if (line_chars + word_length + 1 > max_chars_per_line && line_chars > 0) {
                // Word doesn't fit, move to next line
                x = start_x;
                y += line_height;
                line_chars = 0;
            }
            
            // Draw the word
            while (word_start <= str) {
                tft_draw_char(x, y, *word_start++, color, bg_color);
                x += char_width;
                line_chars++;
            }
            
            word_start = str + 1;  // Next word starts after this space/punctuation
            word_length = 0;
        } else {
            // Part of current word
            word_length++;
        }
        str++;
    }
    
    // Draw any remaining word at the end
    if (word_length > 0) {
        // Check if it fits on current line
        if (line_chars + word_length > max_chars_per_line && line_chars > 0) {
            x = start_x;
            y += line_height;
        }
        
        // Draw the remaining word
        while (*word_start) {
            tft_draw_char(x, y, *word_start++, color, bg_color);
            x += char_width;
        }
    }
}

//////////////////////////////////////////////////////////////////////////////

// Draw a filled circle with specified color
// Parameters: cx, cy = center coordinates, radius = circle radius, color = fill color
void tft_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t color) {
    // Iterate through all pixels in the bounding box of the circle
    for (int16_t y = cy - radius; y <= cy + radius; y++) {
        for (int16_t x = cx - radius; x <= cx + radius; x++) {
            // Calculate distance from center
            int16_t dx = x - cx;
            int16_t dy = y - cy;
            int32_t distance_squared = dx * dx + dy * dy;
            int32_t radius_squared = radius * radius;
            
            // Draw pixel if it's inside the circle
            if (distance_squared <= radius_squared) {
                // Check bounds to avoid drawing outside screen
                if (x >= 0 && x < TFT_WIDTH && y >= 0 && y < TFT_HEIGHT) {
                    tft_draw_pixel(x, y, color);
                }
            }
        }
    }
}

// Draw a filled rectangle (box) with specified color
// Parameters: x0, y0 = top-left corner, x1, y1 = bottom-right corner, color = fill color
void tft_draw_box(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    // Set window to the rectangle area
    tft_set_window(x0, y0, x1, y1);
    
    // Calculate number of pixels
    uint16_t width = x1 - x0 + 1;
    uint16_t height = y1 - y0 + 1;
    uint32_t total_pixels = width * height;
    
    // Fill all pixels with the specified color
    for (uint32_t i = 0; i < total_pixels; i++) {
        send_spi_data16(spi0, color, true);
    }
}

// Draw a line from (x0, y0) to (x1, y1) with specified color
// Uses Bresenham's line algorithm for efficient line drawing
// Parameters: x0, y0 = start point, x1, y1 = end point, color = line color
void tft_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    int16_t dx = abs((int16_t)x1 - (int16_t)x0);
    int16_t dy = abs((int16_t)y1 - (int16_t)y0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    int16_t e2;
    uint16_t x = x0;
    uint16_t y = y0;
    
    while (1) {
        // Draw pixel if within screen bounds
        if (x < TFT_WIDTH && y < TFT_HEIGHT) {
            tft_draw_pixel(x, y, color);
        }
        
        // Check if we've reached the end point
        if (x == x1 && y == y1) {
            break;
        }
        
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

// Draw a thick line from (x0, y0) to (x1, y1) with specified width and color
// Parameters: x0, y0 = start point, x1, y1 = end point, width = line width in pixels, color = line color
void tft_draw_thick_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t width, uint16_t color) {
    // Calculate the perpendicular direction for the width offset
    float dx = (float)x1 - (float)x0;
    float dy = (float)y1 - (float)y0;
    float length = sqrtf(dx * dx + dy * dy);
        
    // Normalize and get perpendicular vector
    float perp_x = -dy / length;
    float perp_y = dx / length;
    
    // Draw multiple parallel lines to create thickness
    int half_width = width / 2;
    for (int offset = -half_width; offset <= half_width; offset++) {
        int offset_x = (int)(perp_x * offset);
        int offset_y = (int)(perp_y * offset);
        tft_draw_line(x0 + offset_x, y0 + offset_y, x1 + offset_x, y1 + offset_y, color);
    }
}

///////////////////////////////////////////////////////////////////////////////

// Display Speed: [value] km/h in a blue box with progress bar
// Parameters: x, y = position of top-left corner of the label box, 
//             speed_str = speed string to display, max_speed = maximum speed for progress bar, all = display mode
void display_speed(uint16_t x, uint16_t y, const char* speed_str, bool all) {
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t blue_color = RGB565(0, 0, 255);
    
    // Draw blue box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, blue_color);
    
    // Print "Speed:" label in the blue box
    tft_print_string(x + 10, y + 8, "Speed:", RGB565(255, 255, 255), blue_color);
    
    // Print the speed value below the box
    tft_print_string(x + 10, y + label_box_height + 10, speed_str, RGB565(0, 0, 0), RGB565(255, 255, 255));

    // Print Units
    tft_print_string(x + 150, y + label_box_height + 10, "km/h", RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Progress bar dimensions
        uint16_t progress_bar_y = y + label_box_height + 100;  // Position below speed text
        uint16_t progress_bar_height = 15;  // Height of progress bar
        uint16_t progress_bar_x_start = x + 10;
        uint16_t progress_bar_x_end = x + box_width - 11;
        uint16_t progress_bar_width = progress_bar_x_end - progress_bar_x_start + 1;
        
        // Draw progress bar background (empty bar in light gray)
        tft_draw_box(progress_bar_x_start, progress_bar_y, progress_bar_x_end, progress_bar_y + progress_bar_height - 1, RGB565(200, 200, 200));
        
        int max_speed = 150;

        // Calculate percentage filled (convert speed_str to float)
        float current_speed = atof(speed_str);
        float percentage = (current_speed / max_speed) * 100.0f;
        if (percentage > 100.0f) percentage = 100.0f;  // Cap at 100%
        if (percentage < 0.0f) percentage = 0.0f;      // Minimum 0%
        
        // Calculate filled width
        uint16_t filled_width = (uint16_t)((percentage / 100.0f) * progress_bar_width);
        
        // Draw filled portion of progress bar in blue (or green/yellow/red based on speed)
        uint16_t progress_color = blue_color;  // Default blue
        if (percentage > 80.0f) {
            progress_color = RGB565(255, 0, 0);  // Red for high speed
        } else if (percentage > 60.0f) {
            progress_color = RGB565(255, 165, 0);  // Orange for medium-high
        } else {
            progress_color = RGB565(0, 255, 0);  // Green for normal speed
        }
        
        if (filled_width > 0) {
            tft_draw_box(progress_bar_x_start, progress_bar_y, 
                        progress_bar_x_start + filled_width - 1, 
                        progress_bar_y + progress_bar_height - 1, progress_color);
        }
        tft_print_string(progress_bar_x_start, progress_bar_y + progress_bar_height + 10, "0", RGB565(0, 0, 0), RGB565(255, 255, 255));
        tft_print_string(progress_bar_x_end - 50, progress_bar_y + progress_bar_height + 10, "150", RGB565(0, 0, 0), RGB565(255, 255, 255));
    }
}

// Display Location: [lat, lon] in a red box
// Parameters: x, y = position of top-left corner of the label box, lat_str = latitude string, lon_str = longitude string
void display_location(uint16_t x, uint16_t y, const char* lat_str, const char* lat_dir, const char* lon_str, const char* lon_dir, bool all) {
    uint16_t line_height = (FONT_HEIGHT * FONT_SCALE) + 4;
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t red_color = RGB565(255, 0, 0);
    
    // Draw red box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, red_color);
    
    // Print "Location:" label in the red box
    tft_print_string(x + 10, y + 8, "Location:", RGB565(255, 255, 255), red_color);
    
    // Print latitude below the box
    tft_print_string(x + 10, y + label_box_height + 10, "Lat: ", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 80, y + label_box_height + 10, lat_str, RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 200, y + label_box_height + 10, lat_dir, RGB565(0, 0, 0), RGB565(255, 255, 255));
    
    // Print longitude below latitude
    tft_print_string(x + 10, y + label_box_height + 10 + line_height, "Lon: ", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 80, y + label_box_height + 10 + line_height, lon_str, RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 200, y + label_box_height + 10 + line_height, lon_dir, RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Print Compass Face
        uint16_t compass_color = RGB565(150, 75, 0);
        tft_draw_circle(120, 200, 80, compass_color);
        tft_print_string(115, 130, "N", RGB565(0, 0, 0),compass_color);
        tft_print_string(175, 195, "E", RGB565(0, 0, 0),compass_color);
        tft_print_string(115, 260, "S", RGB565(0, 0, 0),compass_color);
        tft_print_string(50, 195, "W", RGB565(0, 0, 0),compass_color);

        // Print Direction Line
        // Compass center is at (120, 200) with radius 80
        int center_x = 120;
        int center_y = 200;
        int radius = 60;  // Line length from center
        
        int x_end = center_x;  // Initialize to center (fallback)
        int y_end = center_y;  // Initialize to center (fallback)
        
        // Determine direction based on lat_dir and lon_dir strings
        char lat_char = lat_dir[0];  // Get first character (N or S)
        char lon_char = lon_dir[0];  // Get first character (E or W)
        
        if(lat_char == 'N' && lon_char == 'E'){
            // North-East: x increases (east), y decreases (north)
            x_end = center_x + radius - 10;
            y_end = center_y - radius + 10;
        }
        else if(lat_char == 'N' && lon_char == 'W'){
            // North-West: x decreases (west), y decreases (north)
            x_end = center_x - radius + 10;
            y_end = center_y - radius +10;
        }
        else if(lat_char == 'S' && lon_char == 'E'){
            // South-East: x increases (east), y increases (south)
            x_end = center_x + radius - 10;
            y_end = center_y + radius - 10;
        }
        else if(lat_char == 'S' && lon_char == 'W'){
            // South-West: x decreases (west), y increases (south)
            x_end = center_x - radius + 10;
            y_end = center_y + radius - 10;
        }
        else if(lat_char == 'N'){
            // North only
            x_end = center_x;
            y_end = center_y - radius + 10;
        }
        else if(lat_char == 'S'){
            // South only
            x_end = center_x;
            y_end = center_y + radius - 10;
        }
        else if(lon_char == 'E'){
            // East only
            x_end = center_x + radius - 10;
            y_end = center_y;
        }
        else if(lon_char == 'W'){
            // West only
            x_end = center_x - radius + 10;
            y_end = center_y;
        }
        
        // Draw the direction line from center to calculated endpoint
        tft_draw_thick_line(center_x, center_y, x_end, y_end, 6, red_color);
    }
}

// Display Time: [time_string] in a green box
// Parameters: x, y = position of top-left corner of the label box, time_str = time string to display
void display_time(uint16_t x, uint16_t y, const char* time_str, bool all) {
    uint16_t box_width = 220;
    uint16_t label_box_height = 30;
    uint16_t green_color = RGB565(0, 128, 0);

    int center_x = 120;
    int center_y = 200;
    
    // Draw green box for label
    tft_draw_box(x, y, x + box_width - 1, y + label_box_height - 1, green_color);
    
    // Print "Time:" label in the green box
    tft_print_string(x + 10, y + 8, "Time:", RGB565(255, 255, 255), green_color);
    
    // Parse Time String (assumes format like "123456" or "12:34:56")
    size_t num_substrings = 3;  // HH, MM, SS (not 5)
    char** time_arr = (char**)malloc(num_substrings * sizeof(char*));
    
    // Allocate Memory for Parsed Times
    for (size_t i = 0; i < num_substrings; ++i) {
        time_arr[i] = (char*)malloc(3 * sizeof(char)); // 2 digits + null terminator
        strncpy(time_arr[i], time_str + (i * 2), 2);
        time_arr[i][2] = '\0'; // Null-terminate the substring
    }

    // Print time string below the box
    tft_print_string(x + 10, y + label_box_height + 10, time_arr[0], RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 45, y + label_box_height + 10, ":", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 60, y + label_box_height + 10, time_arr[1], RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 95, y + label_box_height + 10,":", RGB565(0, 0, 0), RGB565(255, 255, 255));
    tft_print_string(x + 110, y + label_box_height + 10, time_arr[2], RGB565(0, 0, 0), RGB565(255, 255, 255));

    if(!all){
        // Print Clock Face
        tft_draw_circle(center_x, center_y, 100, green_color);
        tft_print_string(105, 105, "12", RGB565(255, 255, 255), green_color);
        tft_print_string(155, 120, "1", RGB565(255, 255, 255), green_color);
        tft_print_string(185, 155, "2", RGB565(255, 255, 255), green_color);
        tft_print_string(200, 192, "3", RGB565(255, 255, 255), green_color);
        tft_print_string(185, 229, "4", RGB565(255, 255, 255), green_color);
        tft_print_string(155, 264, "5", RGB565(255, 255, 255), green_color);
        tft_print_string(110, 280, "6", RGB565(255, 255, 255), green_color);
        tft_print_string(65, 264, "7", RGB565(255, 255, 255), green_color);
        tft_print_string(35, 229, "8", RGB565(255, 255, 255), green_color);
        tft_print_string(25, 192, "9", RGB565(255, 255, 255), green_color);
        tft_print_string(35, 155, "10", RGB565(255, 255, 255), green_color);
        tft_print_string(65, 120, "11", RGB565(255, 255, 255), green_color);
        tft_draw_circle(center_x, center_y, 6, RGB565(255, 255, 255));

        // Print Clock Hands
        int time_hour = atoi(time_arr[0]);
        int time_min = atoi(time_arr[1]);
        int time_sec = atoi(time_arr[2]);
        
        // Calculate angles in radians (need to use float/double, not int)
        // Hour hand: 12-hour format, position based on hour + minute fraction
        float hour_angle = ((time_hour % 12) * 30.0f + time_min * 0.5f) * (PI / 180.0f) - (PI / 2.0f);
        // Minute hand: position based on minutes
        float min_angle = (time_min * 6.0f) * (PI / 180.0f) - (PI / 2.0f);
        
        // Calculate hand endpoints (hour hand shorter, minute hand longer)
        int hour_radius = 50;  // Hour hand length
        int min_radius = 70;   // Minute hand length
        
        int x_hour = center_x + hour_radius * cosf(hour_angle);
        int y_hour = center_y + hour_radius * sinf(hour_angle);
        int x_min = center_x + min_radius * cosf(min_angle);
        int y_min = center_y + min_radius * sinf(min_angle);
        
        // Calculate second hand angle (seconds * 6 degrees per second)
        float sec_angle = (time_sec * 6.0f) * (PI / 180.0f) - (PI / 2.0f);
        
        // Calculate second hand endpoint (longest hand)
        int sec_radius = 85;  // Second hand length (longer than minute hand)
        int x_sec = center_x + sec_radius * cosf(sec_angle);
        int y_sec = center_y + sec_radius * sinf(sec_angle);

        // Draw clock hands with different thicknesses
        int hour_hand_width = 4;  // Thicker hour hand
        int min_hand_width = 4;   // Thinner minute hand
        int sec_hand_width = 3;   // Thinnest second hand
        
        // Draw second hand first (longest, goes on bottom layer)
        tft_draw_thick_line(center_x, center_y, x_sec, y_sec, sec_hand_width, RGB565(225, 225, 255)); 
        
        // Draw minute hand second
        tft_draw_thick_line(center_x, center_y, x_min, y_min, min_hand_width, RGB565(255, 255, 255));
        
        // Draw hour hand third (so it appears on top)
        tft_draw_thick_line(center_x, center_y, x_hour, y_hour, hour_hand_width, RGB565(255, 255, 255));
    }

    // Free Memory of Parsed Times
    for (size_t i = 0; i < num_substrings; ++i) {
        free(time_arr[i]);
    }
    free(time_arr);
}

void display_all(const char* speed_str, const char* lat_str, const char* lat_dir, const char* lon_str, const char* lon_dir, const char* time_str){
    display_speed(10, 10, speed_str, 1); // Assuming max_speed is 100.0f for this example
    display_location(10, 100, lat_str, lat_dir, lon_str, lon_dir, 1);
    display_time(10, 210, time_str, 1);
}
