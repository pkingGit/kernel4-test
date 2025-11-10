#include <linux/module.h> // Needed for all kernel modules
#include <linux/kernel.h> // Needed for KERN_INFO
#include <linux/init.h>   // Needed for macros like __init and __exit
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/moduleparam.h>
#include <linux/random.h>

// Game device name
const char* DEVICE_NAME = "tictactoe";
// Game states
const int NOT_STARTED = 0;
const int STARTED     = 1;
const int OVER        = 2;
// Game command results
const char* CANNOT_PLACE     = "CANNOT_PLACE\n";
const char GAME_NOT_STARTED[] = "GAME_NOT_STARTED\n";
const char* GAME_OVER        = "GAME_OVER\n";
const char* GAME_STARTED     = "GAME_STARTED\n";
const char* EMPTY_RESULT     = "";
const char* INVALID_BOT      = "INVALID_BOT\n";
const char* INVALID_COMMAND  = "INVALID_COMMAND\n";
const char* INVALID_FORMAT   = "INVALID_FORMAT\n";
const char* INVALID_PIECE    = "INVALID_PIECE\n";
const char* INVALID_RESET    = "INVALID_RESET\n";
const char* MISSING_PIECE    = "MISSING_PIECE\n";
const char* NOT_CPU_TURN     = "NOT_CPU_TURN\n";
const char* NOT_PLAYER_TURN  = "NOT_PLAYER_TURN\n";
const char* OK               = "OK\n";
const char* OUT_OF_BOUNDS    = "OUT_OF_BOUNDS\n";

/**
 * Structure to store dynamically allocated major and minor device numbers.
 */
static dev_t chr_dev;
/**
 * Structure for game device.
 */
static struct cdev game_cdev; 
/**
 * Structure for device class.
 */
static struct class *device_class;

/**
 * State of tictactoe board.
 */
static char board[3][3];
/**
 * Board command result.
 */
static char board_result[] = ". 1 2 3\n1 _ _ _\n2 _ _ _\n3 _ _ _\n";
/**
 * Game state.
 */
static int game_state = NOT_STARTED;
/**
 * Result of last command.
 */
static char* command_result = (char*)GAME_NOT_STARTED;
/**
 * Player game piece (X or O)
 */
static char player_piece = ' ';
/**
 * Player's turn flag.
 */
static char players_turn = 0;

// Declarations
static void display_board(void);
static int is_game_over(void);
static unsigned int get_random_integer(unsigned int min_value, unsigned int max_value);
static void handle_game_command(char* command);
static void init_board(void);
static void log_board(void);
static void log_command(char* command);
static void rtrim_newline(char* string);
static ssize_t read_device(struct file* filp, char __user *buffer, size_t length, loff_t *offset);
static ssize_t write_device(struct file* filp, const char __user *buffer, size_t length, loff_t *offset);

/**
 * Structure to represent what happens when you read and write to your driver.
 *
 *  Note: you have to pass this in when registering your character driver.
 *        initially, this is not done for you.
 */
static struct file_operations char_driver_ops = {
  .read   = read_device,
  .write  = write_device
};

// You only need to modify the name here.
static struct file_system_type kernel_game_driver = {
  .name     = "tictactoe",
  .fs_flags = O_RDWR
};

/**
 * Initializes and Registers your Module. 
 * You should be registering a character device,
 * and initializing any memory for your project.
 * Note: this is all kernel-space!
 * 
 */
static int __init kernel_game_init(void) {
  int rc;

  // Register character device with kernel
  //int major_number = register_chrdev(0, DEVICE_NAME, &char_driver_ops);
  rc = alloc_chrdev_region(&chr_dev, 0, 1, DEVICE_NAME);
  if (rc != 0){
    printk("ERROR: failed to register character device: errno=%d", rc);
    return rc;
  }

  // Add character device to system
  cdev_init(&game_cdev, &char_driver_ops);
  rc = cdev_add(&game_cdev, chr_dev, 1);
  if (rc != 0){
    printk("ERROR: failed to add character device: errno=%d", rc);
    return rc;
  }

  // Create device class
  device_class = class_create(THIS_MODULE, "kgame_char_class");
  if (IS_ERR(device_class)) {
    // Remove character device from system
    cdev_del(&game_cdev);
    // Unregister character device
    unregister_chrdev_region(chr_dev, 1);
    // Print and return error
    printk("ERROR: Failed to create device, errno=%ld", PTR_ERR(device_class));
    return PTR_ERR(device_class);
  }
  // Create character device
  device_create(device_class, NULL, chr_dev, NULL, DEVICE_NAME);

  // Initialize game board
  init_board();

  // Register file system
  return register_filesystem(&kernel_game_driver);
}

/**
 * Cleans up memory and unregisters your module.
 *  - cleanup: freeing memory.
 *  - unregister: remove your entry from /dev.
 */
static void __exit kernel_game_exit(void) {
  int rc;

  // Unregister file system
  if ((rc = unregister_filesystem(&kernel_game_driver)) != 0){
    // Print error
    printk("ERROR: Failed to unregister file system, errno=%d", rc);
  }

  // Destroy character device
  device_destroy(device_class, chr_dev);
  // Destroy device class
  class_destroy(device_class);
  // Remove character device from system
  cdev_del(&game_cdev);
  // Unregister character device
  unregister_chrdev_region(chr_dev, 1);

  return;
}

/**
 * Reads data from character device and updates offset.
 * @param filp	    device file pointer.
 * @param buffer    buffer to which to write data (in user space).
 * @param length    maximum number of bytes to write.
 * @param offset    offset within device file.
 * @returns number of bytes written or < 0 if error occurred.
 */
static ssize_t read_device(struct file* filp, char __user *buffer, size_t length, loff_t *offset){

  int bytes_read = strlen(command_result);

  // If no command result
  if (bytes_read == 0){
    // Reset offset and return 0 (end-of-file)
    *offset = 0;
    return 0;
  }

  // Copy command result to user space  
  if (copy_to_user(buffer, command_result, bytes_read) != 0){
    // Print error message and return error
    printk("read_device: ERROR: failed to copy data to user space");
    return EIO;
  }

  // Update file position offset (does this make sense?)
  *offset += bytes_read;

  // Clear result (so that next read returns 0)
  command_result = (char*)EMPTY_RESULT;

  // Return success
  return bytes_read;
}

/**
 * Writes data to character device and updates offset.
 * @param filp	    device file pointer.
 * @param buffer    data (in user space) being written.
 * @param length    number of bytes to write.
 * @param offset    offset within device file.
 * @returns number of bytes written or < 0 if error occurred.
 */
static ssize_t write_device(struct file* filp, const char __user *buffer, size_t length, loff_t *offset){
  char* command;

  // Copy data to kernel space
  command = kmalloc(sizeof(char) * length + 1, GFP_KERNEL);
  if (copy_from_user(command, buffer, length) != 0){
    // Print error message and return error
    printk("write_device: ERROR: failed to copy data to kernel space");
    return EIO;
  }
  rtrim_newline(command);

  // Handle game command
  handle_game_command(command);
  log_command(command);

  // Deallocate space for command
  kfree(command);

  // Update file position offset
  //*offset += length;

  // Return success
  return length;
}

/**
 * Handles user game command by updating game state, board, and command result.
 * @param command   user command.
 */
static void handle_game_command(char* command){
  // Process command
  if (strncmp("START", command, 5) == 0){
    char* piece;

    // Validate command
    if (game_state == STARTED){
      // Update command result with error and return
      command_result = (char*)GAME_STARTED;
      return;
    }
    piece = strchr(command, ' ');
    if (piece == NULL){
      // Update command result with error and return
      command_result = (char*)MISSING_PIECE;
      return;
    }
    piece++;
    if (*piece != 'X' && *piece != 'O'){
      // Update command result with error and return
      command_result = (char*)INVALID_PIECE;
      return;
    }

    // Set player piece and turn flag
    player_piece = *piece;
    players_turn = 1;
    // Update game state
    game_state = STARTED;
    // Set command result
    command_result = (char*)OK;

    // Initialize board
    init_board();
  }
  else if (strncmp("RESET", command, 5) == 0){
    // Validate command
    if (strlen(command) > 5){
      command_result = (char*)INVALID_RESET;
      // Update command result with error and return
      return;
    }
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = (char*)GAME_NOT_STARTED;
      return;
    }

    // Reset board
    init_board();

    // Reset game state
    game_state = NOT_STARTED;
    // Reset turn flag
    players_turn = 1;

    // Set command result
    command_result = (char*)OK;
  }
  else if (strncmp("PLAY", command, 4) == 0){
    unsigned int row, col;
    char* position;

    // Validate command
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = (char*)GAME_NOT_STARTED;
      return;
    }
    if (!players_turn){
      // Update command result with error and return
      command_result = (char*)NOT_PLAYER_TURN;
      return;
    }
    position = strchr(command, ' ');
    if (position == NULL){
      // Update command result with error and return
      command_result = (char*)INVALID_FORMAT;
      return;
    }
    position++;
    if (sscanf(position, "%u,%u", &row, &col) != 2){
      // Update command result with error and return
      command_result = (char*)OUT_OF_BOUNDS;
      return;
    }
    if (row < 1 || row > 3 || col < 1 || col > 3){
      // Update command result with error and return
      command_result = (char*)OUT_OF_BOUNDS;
      return;
    }
    // If piece already in given position
    if (board[row - 1][col - 1] != ' '){
      // Update command result with error and return
      command_result = (char*)CANNOT_PLACE;
      return;
    }

    // Place piece
    board[row - 1][col - 1] = player_piece;
    // Update game state and command result
    if (is_game_over()){
      // Update game state and command result
      game_state = OVER;
      command_result = (char*)GAME_OVER;
    }
    else {
      // Update player's turn and command result
      players_turn = !players_turn;
      command_result = (char*)OK;
    }
  }
  else if (strncmp("BOT", command, 3) == 0){
    unsigned int row, col;

    // Validate command
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = (char*)GAME_NOT_STARTED;
      return;
    }
    if (strlen(command) > 3){
      // Update command result with error and return
      command_result = (char*)INVALID_BOT;
      return;
    }
    if (players_turn){
      // Update command result with error and return
      command_result = (char*)NOT_CPU_TURN;
      return;
    }

    // Randomly determine BOT move
    row = get_random_integer(1, 3);
    col = get_random_integer(1, 3);
    while (board[row - 1][col - 1] != ' '){
      row = get_random_integer(1, 3);
      col = get_random_integer(1, 3);
    }

    // Place piece
    board[row - 1][col - 1] = (player_piece == 'X' ? 'O' : 'X');
    // Update game state and command result
    if (is_game_over()){
      // Update game state and command result
      game_state = OVER;
      command_result = (char*)GAME_OVER;
    }
    else {
      // Update player's turn and command result
      players_turn = !players_turn;
      command_result = (char*)OK;
    }
  }
  else if (strcmp("BOARD", command) == 0){
    // Display board
    display_board();
  }
  else {
    command_result = (char*)INVALID_COMMAND;
  }
}

/**
 * Displays the game board, by indicating grid cell content.
 */
static void display_board(void){
    int i, j;

    // Populate game board result
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
        board_result[(i + 1) * 8 + j * 2 + 2] =
          (board[i][j] == ' ' ? '_' : board[i][j]);
      }
    }

    // Update command result
    command_result = board_result;
}

/**
 * Generates random integer within given range, inclusive.
 * @param min_value     lower bound of range.
 * @param max_value     upper bound of range.
 * @returns randomly generated integer within given range, inclusive.
 */
static unsigned int get_random_integer(unsigned int min_value, unsigned int max_value){
  unsigned int value;
  get_random_bytes(&value, sizeof(value));
  return min_value + (value % (max_value - min_value + 1));
}

/**
 * Intializes game board.
 */
static void init_board(void){
  int i, j;

  // Initialize board
  for (i = 0; i < 3; i++){
    for (j = 0; j < 3; j++){
      board[i][j] = ' ';
    }
  }
}

/**
 * Determines whether a player has won the game.
 * @returns 1 if a player has won the game, 0 otherwise.
 */
static int is_game_over(void){
  // Determine if there are 3 identical symbols across, down, or diagonally
  // or there are no more available spots on the board
  return (board[0][0] != ' ' && board[0][0] == board[0][1] && board[0][1] == board[0][2]) ||
         (board[1][0] != ' ' && board[1][0] == board[1][1] && board[1][1] == board[1][2]) ||
         (board[2][0] != ' ' && board[2][0] == board[2][1] && board[2][1] == board[2][2]) ||
         (board[0][0] != ' ' && board[0][0] == board[1][0] && board[1][0] == board[2][0]) ||
         (board[0][1] != ' ' && board[0][1] == board[1][1] && board[1][1] == board[2][1]) ||
         (board[0][2] != ' ' && board[0][2] == board[1][2] && board[1][2] == board[2][2]) ||
         (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) ||
         (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) ||
	 (board[0][0] != ' ' && board[0][1] != ' ' && board[0][2] != ' ' && 
          board[1][0] != ' ' && board[1][1] != ' ' && board[1][2] != ' ' && 
          board[2][0] != ' ' && board[2][1] != ' ' && board[2][2] != ' ');
}

/**
 * Logs the game board, by printing grid cell content.
 */
static void log_board(void){
    int i, j;

    // Populate game board result
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
        board_result[(i + 1) * 8 + j * 2 + 2] =
          (board[i][j] == ' ' ? '_' : board[i][j]);
      }
    }

    // Log board
    printk("kernelgame: board:\n%s", board_result);
}

/**
 * Logs user command, game state, and command result.
 */
static void log_command(char* command){
  printk("kernelgame: command='%s', game_state=%d, players_turn=%d, player_piece='%c', command_result=%s", command, game_state, players_turn, player_piece, command_result);
  if (strncmp("PLAY", command, 4) == 0 || strncmp("BOT", command, 3) == 0 || strncmp("RESET", command, 5) == 0){
    log_board();
  }
}
 
/**
 * Removes trailing newline from string (in place).
 * @param string    a string.
 */
static void rtrim_newline(char *string){
  char *end;

  // Trim trailing newline
  end = string + strlen(string) - 1;
  while (end > string && *end == '\n'){
    end--;
  }

  // Null-terminate the new end of the string
  *(end + 1) = '\0';
}

module_init(kernel_game_init);
module_exit(kernel_game_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Preston King");
MODULE_DESCRIPTION("Kernel tictactoe game for CMSC 421 Project 4.");
