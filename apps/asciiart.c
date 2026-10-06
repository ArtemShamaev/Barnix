#include "barnix_app.h"
#include "../barnixiolib/stdio.h"

// Helper function to print repeated character
void print_repeat_char(char c, int count, int color) {
    char buf[2] = {c, 0};
    for (int i = 0; i < count; i++) {
        barnix->print(color, buf);
    }
}

void print_color(const char *text, int color) {
    barnix->print(color, text);
}

int main(int argc, const char *const *argv) {
    if (argument_count() < 2) {
        puts("Usage: asciiart TEXT");
        puts("Converts text to ASCII art");
        puts("Use --banner for large style");
        return 1;
    }

    const char *text = argument_value(1);
    int banner_style = 0;
    
    // Check for banner flag
    if (argument_count() > 2) {
        const char *style_arg = argument_value(2);
        if (style_arg && style_arg[0] == '-' && style_arg[1] == '-' && style_arg[2] == 'b') {
            banner_style = 1;
        }
    }
    
    if (!text || text[0] == 0) {
        puts("Error: no text provided");
        return 1;
    }
    
    int text_len = 0;
    while (text[text_len]) text_len++;
    
    if (!banner_style) {
        // Simple box style
        int width = text_len + 6;
        
        // Top border
        print_repeat_char('+', 1, 0x0F);
        print_repeat_char('-', width - 2, 0x0F);
        print_repeat_char('+', 1, 0x0F);
        puts("");
        
        // Text line
        print_repeat_char('|', 1, 0x0F);
        putchar(' ');
        puts(text);
        putchar(' ');
        print_repeat_char('|', 1, 0x0F);
        puts("");
        
        // Bottom border
        print_repeat_char('+', 1, 0x0F);
        print_repeat_char('-', width - 2, 0x0F);
        print_repeat_char('+', 1, 0x0F);
        puts("");
        
        puts("ASCII Art: ");
        puts(text);
    } else {
        // Banner style - larger decorative text
        // Top line
        print_repeat_char('=', text_len + 4, 0x0A);
        puts("");
        
        // Text with decorations
        puts("  ");
        print_color(text, 0x0A); // Green text
        puts("  ");
        
        // Decorative line
        putchar(' ');
        print_repeat_char('*', text_len + 2, 0x0C); // Red stars
        putchar(' ');
        puts("");
        
        // Bottom line
        print_repeat_char('=', text_len + 4, 0x0A);
    }
    
    return 0;
}
