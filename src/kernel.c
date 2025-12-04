#include "../include/common.h"
#include "../include/string.h"
#include "../include/cli.h"

//declaration of a few important variables
volatile uint16_t* VGA = (uint16_t*)0xB8000;
uint16_t cursor_row = 0, cursor_col = 0;
static uint8_t color = (0 << 4) | 5;
uint32_t uptime_start = 0;

//headers for timer functions, can be ignored
void hpet_init(void);
double uptime_seconds(void);


//functions that use assembly code, this is how we put things in registers
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

//our timer system
#define HPET_BASE          0xFED00000 
#define HPET_CAP_LOW       (*(volatile uint32_t*)(HPET_BASE + 0x00)) 
#define HPET_CAP_HIGH      (*(volatile uint32_t*)(HPET_BASE + 0x04)) 
#define HPET_CONFIG_LOW    (*(volatile uint32_t*)(HPET_BASE + 0x10)) 
#define HPET_COUNTER_LOW   (*(volatile uint32_t*)(HPET_BASE + 0xF0)) //counter register
#define HPET_COUNTER_HIGH  (*(volatile uint32_t*)(HPET_BASE + 0xF4)) //second counter register, only is used if first overflows

//timer variables
static double hpet_tick_seconds = 0.0;
static double hpet_tick_microseconds = 0.0;
static uint32_t last_high = 0;
static uint32_t overflow_count = 0;

//initialize the timer, creates our two conversion factors
void hpet_init(void) {
    HPET_CONFIG_LOW |= 1;

    uint32_t period_fs = HPET_CAP_HIGH;

    hpet_tick_seconds = (double)period_fs / 1e15;
    
    hpet_tick_microseconds = (double)period_fs / 1e9;

    last_high = HPET_COUNTER_HIGH;
    overflow_count = 0;
}

//if the high register overflows, we track it allowing us to have larger times
void hpet_poll_overflow(void) {
    uint32_t high = HPET_COUNTER_HIGH;

    // If the high part increased, a wrap occurred
    if (high < last_high) {
        overflow_count++;
    }

    last_high = high;
}

uint32_t hpet_get_low(void) {
    return HPET_COUNTER_LOW;
}

//converts the registers values into seconds used for our uptime function
double uptime_seconds(void) {
    hpet_poll_overflow();

    uint32_t low = hpet_get_low();
    uint32_t high = last_high + overflow_count * 0xFFFFFFFF;

    double total_ticks = (double)high * 4294967296.0 + (double)low;
    return total_ticks * hpet_tick_seconds;
}


//converts the registers values into microseconds used for our timer function
double uptime_microseconds(void) {
    hpet_poll_overflow();

    uint32_t low = hpet_get_low();
    uint32_t high = last_high + overflow_count * 0xFFFFFFFF;

    double total_ticks = (double)high * 4294967296.0 + (double)low;
    return total_ticks * hpet_tick_microseconds;
}


void time_delay(int time) {
    uint32_t start = uptime_microseconds();
    uint32_t end = uptime_microseconds();
    while (end - start < time){
        end = uptime_microseconds();
    }
    return;
}



//a few VGA functions that help control printing things properly
void clear_screen() {
    for (int r=0;r<25;r++)
        for (int c=0;c<80;c++)
            VGA[r*80 + c] = (uint16_t)(' ' | ((uint16_t)color << 8));
    cursor_row = cursor_col = 0;
}

void scroll_if_needed() {
    if (cursor_row >= 25) {
        for (int r=1;r<25;r++)
            for (int c=0;c<80;c++)
                VGA[(r-1)*80 + c] = VGA[r*80 + c];
        for (int c=0;c<80;c++)
            VGA[24*80 + c] = (uint16_t)(' ' | ((uint16_t)color << 8));
        cursor_row = 24; cursor_col = 0;
    }
}


//our "print" functions, try to understand how they work as it is something we use a lot, but we will likely only briefly
//cover this in our presentation
void putchar(char ch) {
    if (ch == '\n') { cursor_col=0; cursor_row++; scroll_if_needed(); return; }
    VGA[cursor_row*80 + cursor_col] = (uint16_t)ch | ((uint16_t)color << 8);
    cursor_col++;
    if (cursor_col >= 80) { cursor_col = 0; cursor_row++; scroll_if_needed(); }
}


void puts(const char* s) { while (*s) putchar(*s++); }


void print_uint(uint32_t num) {
    char buf[11]; 
    int i = 10;
    buf[i] = '\0';

    if (num == 0) {
        putchar('0');
        return;
    }

    while (num > 0 && i > 0) {
        buf[--i] = '0' + (num % 10);
        num /= 10;
    }

    puts(&buf[i]);
}


//current keyboard system, we don't have much time to change so understand how it works, I will go
//over it a couple times
static int shift_pressed = 0;

static const char keymap_normal[128] = {
    0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z',
    'x','c','v','b','n','m',',','.','/',0,'*',
    0,0,0,0,0,0,0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char keymap_shift[128] = {
    0,  27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z',
    'X','C','V','B','N','M','<','>','?','*',
    0,0,0,0,0,0,0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


#define KEY_UP     0xE048
#define KEY_DOWN   0xE050
#define KEY_LEFT   0xE04B
#define KEY_RIGHT  0xE04D



int getkey() {
    static int e0_prefix = 0;
    uint8_t sc;

    while (1) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) continue;

        sc = inb(0x60);

        if (sc == 0xE0) {
            e0_prefix = 1;
            continue;
        }

        // Key release
        if (sc & 0x80) {
            sc &= 0x7F;
            if (sc == 42 || sc == 54) shift_pressed = 0;
            e0_prefix = 0;
            continue;
        }

        // Handle shift press
        if (sc == 42 || sc == 54) {
            shift_pressed = 1;
            e0_prefix = 0;
            continue;
        }

        if (e0_prefix) {
            e0_prefix = 0;
            switch (sc) {
                case 0x48: return KEY_UP;
                case 0x50: return KEY_DOWN;
                case 0x4B: return KEY_LEFT;
                case 0x4D: return KEY_RIGHT;
                default: continue;
            }
        }

        // Regular spacebar
        if (sc == 57) return ' ';

        // Normal keys
        if (sc < 128) {
            char c = shift_pressed ? keymap_shift[sc] : keymap_normal[sc];
            if (c != 0) return c;
        }
    }
}





//The user system I created I will cover this briefly, but file system is more important to understand
//and will give a better idea of what this is doing
#define MAX_USERS 8
#define MAX_USERNAME 16
#define MAX_PASSWORD 16

//user structure/class
typedef struct {
    char name[MAX_USERNAME];
    char* password;
} user_t;

static user_t users[MAX_USERS];
static int user_count = 0;


user_t* find_user(const char* name) {
    for (int i=0;i<user_count;i++) if (strcmp(users[i].name, name)==0) return &users[i];
    return 0;
}

void user_create(const char* name, const char* password) {
    if (user_count >= MAX_USERS) return;

    user_t* u = &users[user_count++];

    int i=0; while (i<MAX_USERNAME-1 && name[i]) { u->name[i]=name[i]; i++; } u->name[i]=0;

    int j=0; while (j<MAX_PASSWORD-1 && password[j]) { u->password[j]=password[j]; j++; } u->password[j]=0;

     
}

char* enter_password() {
    static char password[MAX_PASSWORD];
    int index = 0;
    password[0] = 0;

    puts("\nEnter your password: ");

    while (1) {
        char c = getkey();

        if (c == '\n') {
            putchar('\n');
            password[index] = 0;
            return password;
        }

        if (c == '\b') {  
            if (index > 0) {
                index--;
                if (cursor_col > 0) cursor_col--;
                else if (cursor_row > 0) { cursor_row--; cursor_col = 79; }
                VGA[cursor_row * 80 + cursor_col] = (uint16_t)(' ' | ((uint16_t)color << 8));
            }
        } 
        else if (index < MAX_PASSWORD - 1) {
            password[index++] = c;
            putchar('*'); 
        }
    }
}

void user_init() {
    user_create("default", "password");
    user_create("bob", "bob");
}



//Our filesystem, make sure to understand it as it is probably the most directly related to the class
//I will cover this the most 
#define MAX_FILES 64
#define MAX_NAME 16
#define MAX_FILE_SIZE 0x1000 //all files are assumed to be 4kb 
#define FS_START_ADDR 0x400000 //start of the memory we use for our filesystem

//file structure/class
typedef struct {
    char name[MAX_NAME];
    uint8_t *data;
    uint32_t size;
} file_t;
static file_t files[MAX_FILES];
static int file_count = 0;
static uint32_t next_alloc = 0;

//loops through array of files to find one with matching name
file_t* find_file(const char* name) {
    for (int i=0;i<file_count;i++) if (strcmp(files[i].name, name)==0) return &files[i];
    return 0;
}

//creates a file in our ram filesystem
void fs_create(const char* name, const uint8_t* data, uint32_t size) {
    if (file_count >= MAX_FILES || next_alloc >= MAX_FILES) return;

    if (size > MAX_FILE_SIZE) size = MAX_FILE_SIZE;

    file_t* f = &files[file_count++];

    int i=0; while (i<MAX_NAME-1 && name[i]) { f->name[i]=name[i]; i++; } f->name[i]=0;

    uint8_t* mem = (uint8_t*)(FS_START_ADDR + (next_alloc*MAX_FILE_SIZE));
    next_alloc++;

    f->data = mem;
    f->size = size;

    for (uint32_t j=0;j<size;j++) mem[j]=data ? data[j] : 0;

    
    for (uint32_t j = size; j < MAX_FILE_SIZE; j++) mem[j] = 0;
        
}

//adds data to an existing file
void fs_write(file_t* f, const uint8_t* data, uint32_t size) {
    if (!f || !f->data || !data) return;

    if (size > MAX_FILE_SIZE) size = MAX_FILE_SIZE;
    
    for(uint32_t i=0;i < size; i++) f->data[i] = data[i];

    f->size = size;

}

void fs_init() {
    const char *hello = "Welcome to LuxOS!\nType 'help' for commands.\n";
    fs_create("welcome.txt", (const uint8_t*)hello, (uint32_t)45);
    const char *readme = "This is a tiny RAM filesystem. Use 'ls' and 'cat'.\n";
    fs_create("readme.txt", (const uint8_t*)readme, (uint32_t)39);
}


void edit_file(const char* filename) {
    file_t* f = find_file(filename);

    clear_screen();
    puts("=== Simple Text Editor ===\n");
    puts("Type your text. Enter ':save' on a new line to save & exit.\n\n");

    // Display contents
    if (f->data && f->size > 0) {
        for (uint32_t i = 0; i < f->size; i++) {
            putchar(((char*)f->data)[i]);
        }
    }

    char buffer[2048];
    uint32_t len = 0;


    if (f->data && f->size < sizeof(buffer)) {
        for (uint32_t i = 0; i < f->size; i++)
            buffer[len++] = f->data[i];
    }

    char input_line[128];
    int pos = 0;

    while (1) {
        char c = getkey();

        if (c == '\n') {
            input_line[pos] = '\0';

            if (strcmp(input_line, ":save") == 0)
                break;

            
            for (int i = 0; i < pos && len < sizeof(buffer) - 1; i++)
                buffer[len++] = input_line[i];
            buffer[len++] = '\n';

            
            putchar('\n');
            pos = 0;
        }
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                if (cursor_col > 0) cursor_col--;
                VGA[cursor_row * 80 + cursor_col] = (uint16_t)(' ' | ((uint16_t)color << 8));
            }
        }
        else {
            if (pos < sizeof(input_line) - 1) {
                input_line[pos++] = c;
                putchar(c);
            }
        }
    }

    fs_write(f, (uint8_t*)buffer, len);
    puts("\nFile saved and closed.\n");
}



//converts uptime into hours, minutes, seconds
void print_uptime(void) {
    double up = uptime_seconds();
    int total = (int)up;
    int hours = total / 3600;
    int minutes = (total % 3600) / 60;
    int seconds = total % 60;

    puts("Uptime: ");
    print_uint(hours);
    puts(":");
    print_uint(minutes);
    puts(":");
    print_uint(seconds);
    puts("\n");
}





// the start of our command line. also pretty important to understand but fairly simple
//I'll still cover it a bit
#define MAX_INPUT 128
static char input_buffer[MAX_INPUT]; //takes in what is being typed
static int buffer_index = 0;


void splash_screen() {
    puts("+===========================================+\n");
    puts("|   ______ _____  _____  ________________   |\n");
    puts("|   ___  / __  / / /_  |/ /_  __ \\_  ___/   |\n");
    puts("|   __  /  _  / / /__    /_  / / /____ \\    |\n");
    puts("|   _  /___/ /_/ / _    | / /_/ /____/ /    |\n");
    puts("|   /_____/\\____/  /_/|_| \\____/ /____/     |\n");
    puts("+===========================================+\n");
}

//somewhat important, this variable is what i used to track user
const char* prompt = "luxos_root$";

void cli_prompt() { puts(prompt); puts ("> "); }

void run_command(const char* raw_cmd) {
    char cmd[128];
    int i = 0;

    while (*raw_cmd == ' ') raw_cmd++;
    while (*raw_cmd && i < 127) cmd[i++] = *raw_cmd++;
    cmd[i] = 0;

    while (i > 0 && (cmd[i-1]==' '||cmd[i-1]=='\n'||cmd[i-1]=='\r')) cmd[--i]=0;

    if (strcmp(cmd,"")==0) return;




//i can go over how some of these work, but for our presentation it is best if we each are confident in explaining how a
//handful of different ones work


    
    const char* help_args = cmd_args(cmd, "help");
if (help_args) {
    if(!*help_args){
        puts("usage: help <command>\n");
        puts("Available commands:\n");
        puts("  help [command]\n");
        puts("  ls\n");
        puts("  cat <file>\n");
        puts("  echo <text> > <file>\n");
        puts("  touch <file>\n");
        puts("  rm <file>\n");
        puts("  hello\n");
        puts("  clear screen\n");
        puts("  about\n");
        puts("  su\n");
        puts("  showusers\n");
        puts("  adduser\n");
        puts("  timer\n");
        puts("  animation\n");
        puts("  luxosay\n");
        puts("  passwd\n");
        puts("  color\n");
        puts("  rainbow\n");
        puts("  free\n");
        puts("  uptime\n");
        puts("  head\n");
        puts("  tail\n");
        return; }
      
    if(strcmp(help_args, "help") == 0) {puts("help [command] - shows a list of all commands or info about one command.\n");return;}
    if(strcmp(help_args, "ls") == 0) {puts("Lists all files in the RAM filesystem. \n"); return;}
    if(strcmp(help_args, "cat") == 0) {puts("Displays the contents of a file.\n"); return;}
    if(strcmp(help_args, "echo") == 0) {puts("Writes the given text into a new file.\n"); return;}
    if(strcmp(help_args, "touch") == 0) {puts("Creates an empty file with the given name.\n"); return;}
    if(strcmp(help_args, "rm") == 0) {puts("Removes file from RAM Filesystem \n"); return;}
    if(strcmp(help_args, "hello") == 0) {puts("Greets the user \n");return;}
    if(strcmp(help_args, "clear") == 0) { puts("Clears terminal display \n"); return;}
    if(strcmp(help_args, "about") == 0) {puts("Displays information about the OS \n"); return; }
    if(strcmp(help_args, "bug") == 0) {puts("Prints a small ASCII insect\n"); return;}
    if(strcmp(help_args, "su") == 0) {puts("Use: su <username> - Switches users\n"); return;}
    if(strcmp(help_args, "showusers") == 0) {puts("Use: showusers - Prints list of all users\n"); return;}
    if(strcmp(help_args, "addusers") == 0) {puts("Use: addusers - Adds a new user to OS\n"); return;}
    if(strcmp(help_args, "timer") == 0) {puts("Use: timer <command> - Can only be used by root.\n Times how long a function runs. \n"); return;}
    if(strcmp(help_args, "animation") == 0) {puts("Use: animation <number(1-5)> - Select an animation to play.\n"); return;}
    if(strcmp(help_args, "luxosay") == 0) {puts("Use: luxosay <-a> <message> - Displays Luxo saying your message.\n Try different -args to get different eyes.\n"); return;}
    if(strcmp(help_args, "passwd") == 0) {puts("Use: passwd <username> - Changes users password. Must be run by the user or root.\n"); return;}
    if(strcmp(help_args, "color") == 0) {puts("Use: color <color name> - Changes text color to given color.\n"); return;}
    if(strcmp(help_args, "rainbow") == 0) {puts("Use: rainbow <message> - Displays the given message in rainbow text.\n"); return;}
    if(strcmp(help_args, "free") == 0) {puts("Displays amount of free memory in the system.\n"); return;}
    if(strcmp(help_args, "uptime") == 0) {puts("Prints how long the kernel has been running.\n"); return;}
    if(strcmp(help_args, "head") == 0) {puts("Use: head <file> - Displays the first 5 lines of a file.\n"); return;}
    if(strcmp(help_args, "tail") == 0) {puts("Use: tail <file> - Displays the last 5 lines of a file.\n"); return;}
    //add rest of commands
    }


    if (strcmp(cmd,"ls")==0) {
        for (int i=0;i<file_count;i++) { puts(files[i].name); puts("\n"); }
        return;
    }

    if (strcmp(cmd,"showusers")==0) {
        for (int i=0;i<user_count;i++) { puts(users[i].name); puts("\n"); }
        return;
    }


    if(strcmp(cmd,"hello")==0){
        puts("Hello User, how are you? \n");
        return;
    }

    if (strcmp(cmd,"about")==0) {
        puts("Welcome to LuxOS!\n");
        puts("This is an operating system built for learning and fun by Andrew, Hamzeh, and Joseph.\n");
        puts("This OS was built for an OS class and was inspired by our beloved robot Luxo.\n");
        puts("You can explore commands, manage files, and see how an OS works.\n");
        puts("Type 'help' to see what you can do!\n");
        return;
    }

    if (strcmp(cmd, "clear")==0) {
        clear_screen();
        return;
    }


    const char* animation_args = cmd_args(cmd, "animation");
if (animation_args) {
    if(!*animation_args){ puts("usage: animation <number(1-5)>\n"); return; }


    switch (animation_args[0]) {
        case '1':
            clear_screen();
            puts("                     ___\n o__        o__     |   |\\ \n/|          /\\      |   |X\\ \n/ > o        <\\     |   |XX\\ \n");
            time_delay(500000);
            clear_screen();
            puts("                     ___\n o__        o__     |   |\\ \n/|          /\\      |   |X\\ \n/ >  o       <\\     |   |XX\\ \n");
            time_delay(500000);
            clear_screen();
            puts("                     ___\n o__        o__     |   |\\ \n/|          /\\      |   |X\\ \n/ >    o     <\\     |   |XX\\ \n");
            time_delay(500000);
            clear_screen();
            puts("                     ___\n o__        o__     |   |\\ \n/|          /\\      |   |X\\ \n/ >      o   <\\     |   |XX\\ \n");
            break;

        case '2':
            clear_screen();
            puts(" ____\n|___ \\\n  __) |\n |__ <\n ___) |\n|____/\n"); 
            time_delay(1000000);
            clear_screen();
            puts(" ___\n|__ \\\n   ) |\n  / /\n / /_\n|____|\n");
            time_delay(1000000);
            clear_screen();
            puts(" __\n/_ |\n | |\n | |\n | |\n |_|\n");
            time_delay(1000000);
            clear_screen();
            puts("      _ ._  _ , _ ._\n    (_ ' ( `  )_  .__)\n  ( (  (    )   `)  ) _)\n (__ (_   (_ . _) _) ,__)\n     `~~`\\ ' . /`~~`\n          ;   ;\n          /   \\\n_________/_ __ \\_________\n");
            break;

        case '3':
            clear_screen();
            puts("  |\\_/|\n /     \\\n|       |\n|       |\n|       |\n \\     /\n  |___|\n");
            time_delay(1000000);
            clear_screen();
            puts("  |\\_/|\n / o o \\\n|       |\n|  \\_/  |\n|       |\n \\     /\n  |___|\n");
            time_delay(1000000);
            clear_screen();
            puts("  |\\_/|\n / ^ ^ \\\n|       |\n|  \\_/  |\n|       |\n \\     /\n  |___|\n");
            break;

        case '4':
            for (int i=0;i<45;i+=2){
                clear_screen();
                for (int j=0;j<i;j++) putchar(' ');
                puts("      0 @ 0 @\n");
                for (int j=0;j<i;j++) putchar(' ');
                puts("    ____      0\n");
                for (int j=0;j<i;j++) putchar(' ');
                puts("___ |[]|_n__n_I_c\n");
                for (int j=0;j<i;j++) putchar(' ');
                puts("|___||__|###|____}\n");
                for (int j=0;j<i;j++) putchar(' ');
                puts(" O-o--O-o+++--O-o\n");
                time_delay(200000);
                clear_screen();
                for (int j=0;j<i+1;j++) putchar(' ');
                puts("      @ 0 @ 0\n");
                for (int j=0;j<i+1;j++) putchar(' ');
                puts("    ____      @\n");
                for (int j=0;j<i+1;j++) putchar(' ');
                puts("___ |[]|_n__n_I_c\n");
                for (int j=0;j<i+1;j++) putchar(' ');
                puts("|___||__|###|____}\n");
                for (int j=0;j<i+1;j++) putchar(' ');
                puts(" o-O--o-O+++--o-O\n");
                time_delay(250000);
            }
            break;

        case '5':
            const char* ship[] = {
            "       .\n"
            "       .\n",
            "       .\n",
            "      ( )\n",
            "     (( ))\n",
            "    ((   ))\n",
            "   ((  :  ))\n",
            "   ((  :  ))\n",
            "    ((   ))\n",
            "|/   ( | )   \\|\n",
            "| /  ( | )  \\ |\n",
            "|  / ^ | ^ \\  |\n",
            " /  |##!##|  \\\n",
            "  / |##!##| \\\n",
            "   /|##!##|\\\n",
            "    |     |\n",
            "    |     |\n",
            "    |=   =|\n",
            "     /___\\\n",
            "      / \\\n",
            "       ^\n",
            "       |\n",
            "       |\n", 


        };


        int ship_height = sizeof(ship) / sizeof(ship[0]);


        for (int offset = 0; offset <= 20; offset++) {
            clear_screen();


    


            for (int i = ship_height; i > 0; i--){puts(ship[i]);}
            for (int i = 0; i < offset; i++){
                putchar('\n');
            }
            
            ship_height = ship_height-1;


        
            
            
            time_delay(500000);
            
        }


        clear_screen();
        puts("Liftoff complete!\n");
        break;

    }
    return;
}

    if (strcmp(cmd,"bug")==0) {
        for (int i=0;i<4;i++){
            switch(i){
                case 0:
                    puts("123\n1sfad\n");
                    time_delay(500000);
                    puts("    \\( )/\n");
                    puts("     ( ) \n");
                    time_delay(1000000);
                    puts("adggg\nvkgj\n99jdh");
                    time_delay(250000);
                    clear_screen();
                    break;
                case 1:
                    puts("1113$$11\\91*1**\n");
                    time_delay(500000);
                    puts("1#####*<s[{{(&!!!!\n");
                    time_delay(500000);
                    puts("11adsfadsf1\\91*1**\n");
                    time_delay(500000);
                    break;

                case 2:
                    puts("dsafadsf\n");
                    time_delay(500000);
                    puts("    \\( )/\n");
                    puts("       ASDDDDDrTBGETHGSDVFEGWDQf\n");
                    time_delay(1000000);
                    puts("    -( )-\n");
                    time_delay(3000000);
                    clear_screen();
                    break;


                case 3:
                    puts("    \\( )/\n");
                    puts("      )  \n");
                    time_delay(1000000);
                    puts("adggg\nvkgj\n99jdh");
                    time_delay(250000);
                    clear_screen();
                    break;
                
                default:
                    puts("123\n1sfad\n");
                    time_delay(500000);
                    puts("    \\( )/\n");
                    puts("     ( ) \n");
                    time_delay(1000000);
                    puts("adggg\nvkgj\n99jdh");
                    time_delay(2500000);
                    clear_screen();
                    break;
            }
        }
        puts("    \\( )/\n");
        puts("    -( )-\n");
        return;
    }

    if (strcmp(cmd, "easter egg")==0) {
        puts("        ___\n");
        puts("     .-*)) `*-.\n");
        puts("    /*  ((*   *'.\n");
        puts("   |   *))  *   *\\\n");
        puts("   | *  ((*   *  /\n");
        puts("    \\  *))  *  .'\n");
        puts("     '-.((*_.-'\n");
        return;
    }
    

    //this switches users
    const char* su_args = cmd_args(cmd, "su");
if (su_args) {
    if(!*su_args){ puts("usage: su <username/root>\n"); return; }
    if(strcmp(su_args, "root") == 0) {  //if switching to root no password prompt
        prompt = "luxos_root$"; //as i said this prompt varable tracks user
        return;
    }

    user_t* u = find_user(su_args); //checks if there is a user with name
    if(!u){ puts("user not found\n"); return; } //if not then quit

    if (strcmp(u->password, enter_password()) == 0) //otherwise prompt for password and check
    {
        prompt = u->name;
        return;
    } else { 
        puts("Incorrect Password\n");
        return;
    }
    
    return;

}

    //adding users
    const char* adduser_args = cmd_args(cmd, "adduser");
if (adduser_args) {
    if(!*adduser_args){puts("usage: adduser <username>\n"); return;}

    if (strcmp(prompt, "luxos_root$") != 0){ //checks if root, otherwise will not work
        puts("You do not have permissions to run this command\n");
        return;
    }
    if (find_user(adduser_args)) { //checks if user already exists
        puts("User already exists\n");
        return;
    }

    //adds password to user
    const char* pwd = enter_password();
    const char* pwd2 = enter_password();

    if(strcmp(pwd, pwd2)!=0)
    {
        puts("passwords do not match");
        return;
    }

    user_create(adduser_args, pwd);
    return;
}

    //changes passwords
    const char* passwd_args = cmd_args(cmd, "passwd");
if(passwd_args) {
    if(!*passwd_args) {puts("usage: passwd <username>\n"); return; }

    
    user_t* u = find_user(passwd_args); //checks if user exists
    if(!u){ puts("user not found\n"); return; }

    if(strcmp(prompt, passwd_args) != 0 && strcmp(prompt, "luxos_root$") != 0) //must be the current user or root
    {
        puts("You do not have permission to change this password\n");
        return;
    }

    if(strcmp(prompt, "luxos_root$") != 0)
    {
        puts("Enter Current Password");
        const char* current_pwd = enter_password();

        if(strcmp(u->password, current_pwd) != 0){
            puts("Incorrect current password");
        }
    }

    char* pwd1 = enter_password();
    char* pwd2 = enter_password();

    if(strcmp(pwd1, pwd2)!=0)
    {
        puts("passwords do not match");
        return;
    }

    u->password = pwd1;

    return;
    

}

    const char* color_args = cmd_args(cmd, "color");
if (color_args) {
    if(!*color_args){ puts("usage: color <color name>\n"); return; }
    if(strcmp(color_args, "green") == 0) {
        color = (0 << 4) | 2;
    } 
    if (strcmp(color_args, "red") == 0) {
        color = (0 << 4) | 4;
    } 
    if (strcmp(color_args, "blue") == 0) {
        color = (0 << 4) | 1;
    }
    if (strcmp(color_args, "cyan") == 0) {
        color = (0 << 4) | 3;
    }
    if (strcmp(color_args, "magenta") == 0) {
        color = (0 << 4) | 5;
    }
    if (strcmp(color_args, "yellow") == 0) {
        color = (0 << 4) | 14;
    }
    if (strcmp(color_args, "brown") == 0) {
        color = (0 << 4) | 6;
    } 
    if (strcmp(color_args, "white") == 0) {
        color = (0 << 4) | 7;
    }
    
    return;
}


const char* luxo_args = cmd_args(cmd, "luxosay");
if (luxo_args) {
    if(!*luxo_args){
        puts("usage: luxosay [-x] <message>\n");
        return;
    }

    char mode = 'a';
    const char* msg = luxo_args;

    if (luxo_args[0] == '-' && luxo_args[1] != 0) {
        mode = luxo_args[1];
        msg = luxo_args + 2; 
        while (*msg == ' ') msg++; 
    }


    puts("                      <");
    puts(msg);
    puts(">\n");
    puts("          |______|       /\n");
    puts("          |.    .|      /\n");
    puts("          |. [] .|\n");   
    puts("          |______|\n");      
    puts(" _____      |  |      _____\n");
    puts(" |   |__[----------]__|   |\n");


    switch (mode) {
        case 'a':
            puts(" |   |__|  o    o  |__|   |\n");
            break;

        case 'd':
            puts(" |   |__|  X    X  |__|   |\n");
            break;

        case 't':
            puts(" |   |__|  _    _  |__|   |\n");
            break;

        case 'p':
            puts(" |   |__|  @    @  |__|   |\n");
            break;

        case 's':
            puts(" |   |__|  *    *  |__|   |\n");
            break;

        case 'g':
            puts(" |   |__|  $    $  |__|   |\n");
            break;
        
        case 'b':
            puts(" |   |__|  =    =  |__|   |\n");
            break;

        case 'y':
            puts(" |   |__|  .    .  |__|   |\n");
            break;

        case 'w':
            puts(" |   |__|  O    O  |__|   |\n");
            break;
            
        default: 
            puts(" |   |__|  ?    ?  |__|   |\n");
            break;

    }
            




    puts(" |   |__[----------]__|   |\n");
    puts(" _____                _____\n");
    return;
}



    const char* rainbow_args = cmd_args(cmd, "rainbow");
if(rainbow_args) {
    if(!*rainbow_args){puts("usage: rainbow <text>"); return;}

    uint8_t og_color = color;

    for (int i = 0; i < strlen(rainbow_args); i++)
    {
        switch (i%7)
        {
            case 0:
                color = (0 << 4) | 4;
                putchar(rainbow_args[i]);
                break;

            case 1:
                color = (0 << 4) | 6;
                putchar(rainbow_args[i]);
                break;

            case 2:
                color = (0 << 4) | 14;
                putchar(rainbow_args[i]);
                break;

            case 3:
                color = (0 << 4) | 2;
                putchar(rainbow_args[i]);
                break;

            case 4:
                color = (0 << 4) | 3;
                putchar(rainbow_args[i]);
                break;

            case 5:
                color = (0 << 4) | 9;
                putchar(rainbow_args[i]);
                break;

            case 6:
                color = (0 << 4) | 5;
                putchar(rainbow_args[i]);
                break;
            
            default:
                break;
        }
    }
    color = og_color;
    putchar('\n');
    return;


}



    const char* cat_args = cmd_args(cmd, "cat");
if (cat_args) {
    if(!*cat_args){ puts("usage: cat <file>\n"); return; }
    file_t* f = find_file(cat_args);
    if(!f){ puts("file not found\n"); return; }
    puts((char*)f->data);
    putchar('\n');
    return;
}

    const char* head_args = cmd_args(cmd, "head");
if (head_args) {
    if(!*head_args){ puts("usage: head <file>\n"); return; }
    file_t* f = find_file(head_args);
    if(!f){ puts("file not found\n"); return; }

    int lines = 0;
    for (uint32_t i = 0; i < f->size; i++) {
        putchar(((char*)f->data)[i]);
        if (((char*)f->data)[i] == '\n') {
            lines++;
            if (lines >= 5) break;
        }
    }
    return;
}

    const char* tail_args = cmd_args(cmd, "tail");
if (tail_args) {
    if(!*tail_args){ puts("usage: tail <file>\n"); return; }
    file_t* f = find_file(tail_args);
    if(!f){ puts("file not found\n"); return; }

    int lines = 0;
    for (uint32_t i = 0; i < f->size; i++) {
        if (((char*)f->data)[i] == '\n') {
            lines++;
        }
    }
    int start_line = lines - 5;;
    if (start_line < 0) start_line = 0;
    int current_line = 0;
    for (uint32_t i = 0; i < f->size; i++) {
        if (current_line >= start_line) {
            putchar(((char*)f->data)[i]);
        }
        if (((char*)f->data)[i] == '\n') {
            current_line++;
        }
    }
    return;
}


    const char* touch_args = cmd_args(cmd, "touch");
if (touch_args) {
    if(!*touch_args){ puts("usage: touch <file>\n"); return; }
    fs_create(touch_args, 0, 0);
    return;
}


    const char* rm_args = cmd_args(cmd, "rm");
if (rm_args) {
    if(!*rm_args){ puts("usage: rm <file>\n"); return; }
    for(int i=0;i<file_count;i++){
        if(strcmp(files[i].name,rm_args)==0){
            for(int k=i;k<file_count-1;k++) files[k]=files[k+1];
            file_count--;
            return;
        }
    }
    puts("file not found\n");
    return;
}


    const char* start = cmd_args(cmd, "echo");
if (start) {
    const char* arrow = cmd; const char* gt=0;
    while(*arrow){ if(*arrow=='>'){ gt=arrow; break; } arrow++; }
    if(!gt){ puts("usage: echo <text> > <file>\n"); return; }

    const char* fn=gt+1; while(*fn==' ') fn++;
    if(!*fn){ puts("no filename\n"); return; }

    char name[32]; int ni=0;
    while(*fn && *fn!=' ' && ni<31) {
        name[ni++]=*fn++; 
    }
    

    char textbuf[512]; int ti=0;
    while(*start==' ') start++;
    const char* end=gt; while(start<end && ti<511) textbuf[ti++]=*start++;
    textbuf[ti]=0;

    file_t* f = find_file(name);
    if(!f) fs_create(name,(const uint8_t*)textbuf,ti);
    else{
        name[ni+1]=0; name[ni+2]=0; 
        fs_create(name,(const uint8_t*)textbuf,ti);
    }
    return;
}

    const char* edit_args = cmd_args(cmd, "edit");
if (edit_args) {
    if (!*edit_args) {
        puts("usage: edit <filename>\n");
        return;
    }

    file_t* f = find_file(edit_args);
    if (!f) {
        puts("Use echo or touch to create file first!\n");
        return;
    }

    edit_file(edit_args);
    return;

}


if(strcmp(cmd,"free")==0) {
    int size;
    for (int i=0;i<file_count;i++) { size = size + 1; }
    print_uint(size * 1000);
    puts("/n");
    return;
}

if (strcmp(cmd,"uptime")==0) {
        print_uptime();
        return;
    }


    const char* timer_args = cmd_args(cmd, "timer");
if (timer_args ) {
    if (!*timer_args) {
        puts("usage: timer <command>\n");
        return;
    }
    if (strcmp(prompt, "luxos_root$") != 0){ //also requires root
        puts("You do not have permissions to run this command\n");
        return;
    }
    uint32_t start = uptime_microseconds();

    run_command(timer_args);

    uint32_t end = uptime_microseconds();

    uint32_t s = end - start;
    puts("Command took ");
    print_uint(s);
    puts(" microseconds\n");
    return;
}




    const char* average_args = cmd_args(cmd, "average");
if (average_args ) {
    if (!*average_args) {
        puts("usage: timer <command>\n");
        return;
    }
    if (strcmp(prompt, "luxos_root$") != 0){ //also requires root
        puts("You do not have permissions to run this command\n");
        return;
    }

    uint32_t average_s = 0; 
    for (int j = 0; j < 10; j++) {
        uint32_t start = uptime_microseconds();

        run_command(average_args);

        uint32_t end = uptime_microseconds();

        uint32_t s = end - start;
        average_s += s;
    }
    
    puts("Command took an average of ");
    print_uint(average_s / 10);
    puts(" microseconds\n");
    return;
}



    puts("unknown command\n");
}


//command history that allows me to scroll through previous commands you can ignore, its not important
#define HISTORY_SIZE 15
#define MAX_CMD_LEN 128

static char command_history[HISTORY_SIZE][MAX_CMD_LEN];
static int history_count = 0;
static int history_index = -1;  


void add_to_history(const char* cmd) {
    if (cmd[0] == 0) return;

    if (history_count < HISTORY_SIZE) {
        for (int i = 0; cmd[i] && i < MAX_CMD_LEN - 1; i++)
            command_history[history_count][i] = cmd[i];
        command_history[history_count][MAX_CMD_LEN - 1] = '\0';
        history_count++;
    } else {
        for (int i = 1; i < HISTORY_SIZE; i++)
            for (int j = 0; j < MAX_CMD_LEN; j++)
                command_history[i - 1][j] = command_history[i][j];

        for (int i = 0; cmd[i] && i < MAX_CMD_LEN - 1; i++)
            command_history[HISTORY_SIZE - 1][i] = cmd[i];
        command_history[HISTORY_SIZE - 1][MAX_CMD_LEN - 1] = '\0';
    }
}

void recall_command(int index, char* buffer, int* buf_index) {
    while (*buf_index > 0) {
        putchar('\b');
        *buf_index -= 1;
    }

    int i = 0;
    while (command_history[index][i] && i < MAX_CMD_LEN - 1) {
        buffer[i] = command_history[index][i];
        putchar(buffer[i]);
        i++;
    }
    buffer[i] = '\0';
    *buf_index = i;
}


// the main kernel code that runs in a infinite loop
//obviously pretty simple but important to know how it works 
void kernel_main() {
    //starts timer
    hpet_init();

    clear_screen();
    //puts our splash screen
    splash_screen();

    //adds our preset files and users
    user_init();
    fs_init();
    file_t* welcome = find_file("welcome.txt");
    if(welcome && welcome->data)
        for(uint32_t i=0;i<welcome->size;i++) putchar((char)welcome->data[i]);

    buffer_index=0; input_buffer[0]=0;


    putchar('\n');

    cli_prompt();
 
    //infinite loop that takes input and runs commands
    while(1){

        char c = getkey();

        if (c == 'H') {
            while (buffer_index  > 0) {
                buffer_index--;
                if (cursor_col > 0) cursor_col--;
                else if (cursor_row > 0) {
                    cursor_row--;
                    cursor_col = 79;
                }
                VGA[cursor_row * 80 + cursor_col] = (uint16_t)(' ' | (color << 8));
            }
            if (history_count > 0) {
                if (history_index == -1)
                    history_index = history_count - 1;
                else if (history_index > 0)
                    history_index--;
                recall_command(history_index, input_buffer, &buffer_index);
            }
        }
        else if (c == 'P') {
            while (buffer_index > 0) {
                buffer_index--;
                if (cursor_col > 0) cursor_col--;
                else if (cursor_row > 0) {
                    cursor_row--;
                    cursor_col = 79;
                }
                VGA[cursor_row * 80 + cursor_col] = (uint16_t)(' ' | (color << 8));
            }
            if (history_index != -1) {
                history_index++;
                if (history_index >= history_count) {
                    history_index = -1;
                    while (buffer_index > 0) {
                        putchar('\b');
                        buffer_index--;
                    }
                    input_buffer[0] = 0;
                } else {
                    recall_command(history_index, input_buffer, &buffer_index);
                }
            }
        }
        else if (c == '\n') {
            putchar('\n');
            input_buffer[buffer_index] = 0;
            add_to_history(input_buffer);
            history_index = -1;
            run_command(input_buffer);
            buffer_index = 0;
            input_buffer[0] = 0;
            cli_prompt();
        }
        else if (c == '\b') {
            if (buffer_index > 0) {
                buffer_index--;
                if (cursor_col > 0) cursor_col--;
                else if (cursor_row > 0) {
                    cursor_row--;
                    cursor_col = 79;
                }
                VGA[cursor_row * 80 + cursor_col] = (uint16_t)(' ' | (color << 8));
            }
        }
        else {
            if (buffer_index < MAX_INPUT - 1) {
                input_buffer[buffer_index++] = c;
                putchar(c);
            }
        }


    }
}


