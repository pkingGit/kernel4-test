#include <linux/module.h> // Needed for all kernel modules
#include <linux/kernel.h> // Needed for KERN_INFO
#include <linux/init.h>   // Needed for macros like __init and __exit
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
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
const char* CANNOT_PLACE     = "CANNOT_PLACE";
const char GAME_NOT_STARTED[] = "GAME_NOT_STARTED";
const char* GAME_OVER        = "GAME_OVER";
const char* GAME_STARTED     = "GAME_STARTED";
const char* INVALID_BOT      = "INVALID_BOT";
const char* INVALID_COMMAND  = "INVALID_COMMAND";
const char* INVALID_FORMAT   = "INVALID_FORMAT";
const char* INVALID_PIECE    = "INVALID_PIECE";
const char* INVALID_RESET    = "INVALID_RESET";
const char* MISSING_PIECE    = "MISSING_PIECE";
const char* NOT_CPU_TURN     = "NOT_CPU_TURN";
const char* NOT_PLAYER_TURN  = "NOT_PLAYER_TURN";
const char* OK               = "OK";
const char* OUT_OF_BOUNDS    = "OUT_OF_BOUNDS";

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
static char board_result[33] = ". 1 2 3\n1      \n2      \n3      \n\0";
/**
 * Game state.
 */
static int game_state = NOT_STARTED;
/**
 * Result of last command.
 */
static char* command_result = GAME_NOT_STARTED;
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
static void handle_game_command(char* command);
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
  device_create(device_class, NULL, chr_dev, NULL, "kgame_char_device");

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
 * @param filp		device file pointer.
 * @param buffer	buffer to which to write data (in user space).
 * @param length	maximum number of bytes to write.
 * @param offset	offset within device file.
 * @returns number of bytes written or < 0 if error occurred.
 */
static ssize_t read_device(struct file* filp, char __user *buffer, size_t length, loff_t *offset){

  int bytes_read = strlen(command_result);

  // Copy command result to user space  
  if (copy_to_user(buffer, command_result, bytes_read) != 0){
    // Print error message and return error
    printk("read_device: ERROR: failed to copy data to user space");
    return EIO;
  }

  // Update file position offset (does this make sense?)
  *offset += bytes_read;

  // Return success
  return bytes_read;
}

/**
 * Writes data to character device and updates offset.
 * @param filp		device file pointer.
 * @param buffer	data (in user space) being written.
 * @param length	number of bytes to write.
 * @param offset	offset within device file.
 * @returns number of bytes written or < 0 if error occurred.
 */
static ssize_t write_device(struct file* filp, const char __user *buffer, size_t length, loff_t *offset){
  // Copy data to kernel space
  char* command = kmalloc(sizeof(char) * length + 1, GFP_KERNEL);
  if (copy_from_user(command, buffer, length) != 0){
    // Print error message and return error
    printk("write_device: ERROR: failed to copy data to kernel space");
    return EIO;
  }

  // Handle game command
  handle_game_command(command);

  // Deallocate space for command
  kfree(command);

  // Update file position offset
  *offset += length;

  // Return success
  return 0;
}

/**
 * Handles user game command by updating game state, board, and command result.
 * @param command	user command.
 */
static void handle_game_command(char* command){

  // Process command
  if (strncmp("START", command, 5) == 0){
    char* piece;
    int i, j;

    // Validate command
    if (game_state == STARTED){
      // Update command result with error and return
      command_result = GAME_STARTED;
      return;
    }
    piece = strchr(command, ' ');
    if (piece == NULL){
      // Update command result with error and return
      command_result = MISSING_PIECE;
      return;
    }
    piece++;
    if (*piece != 'X' && *piece != 'O'){
      // Update command result with error and return
      command_result = INVALID_PIECE;
      return;
    }

    // Set player piece and turn flag
    player_piece = *piece;
    players_turn = 1;
    // Update game state
    game_state = STARTED;
    // Set command result
    command_result = OK;

    // Initialize board
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
        board[i][j] = ' ';
      }
    }
  }
  else if (strncmp("RESET", command, 5) == 0){
    int i, j;

    // Validate command
    if (strlen(command) > 5){
      command_result = INVALID_RESET;
      // Update command result with error and return
      return;
    }
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = GAME_NOT_STARTED;
      return;
    }

    // Reset board
    for (i = 0; i < 3; i++){
      for (j = 0; j < 3; j++){
        board[i][j] = ' ';
      }
    }

    // Reset game state
    game_state = NOT_STARTED;

    // Set command result
    command_result = OK;
  }
  else if (strncmp("PLAY", command, 4) == 0){
    int row, col;
    char* position;

    // Validate command
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = GAME_NOT_STARTED;
      return;
    }
    if (!players_turn){
      // Update command result with error and return
      command_result = NOT_PLAYER_TURN;
      return;
    }
    position = strchr(command, ' ');
    if (position == NULL){
      // Update command result with error and return
      command_result = INVALID_FORMAT;
      return;
    }
    position++;
    if (sscanf(position, "%d,%d", &row, &col) != 2){
      // Update command result with error and return
      command_result = OUT_OF_BOUNDS;
      return;
    }
    if (row < 1 || row > 3 || col < 1 || col > 3){
      // Update command result with error and return
      command_result = OUT_OF_BOUNDS;
      return;
    }
    // If piece already in given position
    if (board[row][col] != ' '){
      // Update command result with error and return
      command_result = CANNOT_PLACE;
      return;
    }

    // Place piece
    board[row][col] = player_piece;
    // Update game state and command result
    if (is_game_over()){
      game_state = OVER;
      command_result = GAME_OVER;
    }
    else {
      command_result = OK;
    }
  }
  else if (strncmp("BOT", command, 3) == 0){
    int row, col;

    // Validate command
    if (game_state != STARTED){
      // Update command result with error and return
      command_result = GAME_NOT_STARTED;
      return;
    }
    if (strlen(command) > 3){
      // Update command result with error and return
      command_result = INVALID_BOT;
      return;
    }
    if (players_turn){
      // Update command result with error and return
      command_result = NOT_CPU_TURN;
      return;
    }

    // Randomly determine BOT move
    get_random_bytes(&row, sizeof(row));
    get_random_bytes(&col, sizeof(col));
    while (board[row][col] != ' '){
    }

    // Place piece
    board[row][col] = (player_piece == 'X' ? 'O' : 'X');
    // Update game state and command result
    if (is_game_over()){
      game_state = OVER;
      command_result = GAME_OVER;
    }
    else {
      command_result = OK;
    }
  }
  else if (strcmp("BOARD", command) == 0){
    // Display board
    display_board();
  }
  else {
    command_result = INVALID_COMMAND;
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
        board_result[(i + 1) * 8 + j * 2 + 2] = board[i][j];
      }
    }

    // Update command result
    command_result = board_result;
}

/**
 * Determines whether a player has won the game.
 * @returns 1 if a player has won the game, 0 otherwise.
 */
static int is_game_over(void){
  // Determine if there are 3 identical symbols across, down, or diagonally
  return (board[0][0] == board[0][1] && board[0][1] == board[0][2]) ||
         (board[1][0] == board[1][1] && board[1][1] == board[1][2]) ||
         (board[2][0] == board[2][1] && board[2][1] == board[2][2]) ||
         (board[0][0] == board[1][0] && board[1][0] == board[2][0]) ||
         (board[0][1] == board[1][1] && board[1][1] == board[2][1]) ||
         (board[0][2] == board[1][2] && board[1][2] == board[2][2]) ||
         (board[0][0] == board[1][1] && board[1][1] == board[2][2]) ||
         (board[0][2] == board[1][1] && board[1][1] == board[2][0]);
}

module_init(kernel_game_init);
module_exit(kernel_game_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Preston King");
MODULE_DESCRIPTION("Kernel tictactoe game for CMSC 421 Project 4.");
