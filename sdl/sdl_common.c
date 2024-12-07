//
// Created by Mariotaku on 2021/10/14.
//

#include "sdl_common.h"

#if USE_SDL || USE_SDL_GPU
#include "sdl_common_internal.h"

/*********************
 *      DEFINES
 *********************/

#ifndef KEYBOARD_BUFFER_SIZE
#define KEYBOARD_BUFFER_SIZE SDL_TEXTINPUTEVENT_TEXT_SIZE
#endif

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    bool left_button_down;
    int16_t last_x;
    int16_t last_y;
} mouse_state_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/


/**********************
 *  STATIC VARIABLES
 **********************/

volatile bool sdl_quit_qry = false;

static mouse_state_t mouse_state_1 = {false, 0, 0};

#if SDL_DUAL_DISPLAY
static mouse_state_t mouse_state_2 = {false, 0, 0};
#endif

static int16_t wheel_diff = 0;
static lv_indev_state_t wheel_state = LV_INDEV_STATE_RELEASED;

static char buf[KEYBOARD_BUFFER_SIZE];

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
/**
 * Get the current position and state of the mouse
 * @param indev_drv pointer to the related input device driver
 * @param data store the mouse data here
 */
void sdl_mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    lv_disp_t *disp = indev_drv->disp;

    mouse_state_t * mouse_state;
    
    if (disp == NULL) { // NULL is the default display
        mouse_state = &mouse_state_1;
    }
    if (sdl_dis_drv_is_monitor_1(disp->driver)) {
        mouse_state = &mouse_state_1;
    }
    #if SDL_DUAL_DISPLAY
    else if (sdl_dis_drv_is_monitor_2(disp->driver)) {
        mouse_state = &mouse_state_2;
    }
    #endif

    if (mouse_state == NULL) return;

    data->point.x = mouse_state->last_x;
    data->point.y = mouse_state->last_y;
    data->state = mouse_state->left_button_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}


/**
 * Get encoder (i.e. mouse wheel) ticks difference and pressed state
 * @param indev_drv pointer to the related input device driver
 * @param data store the read data here
 */
void sdl_mousewheel_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void) indev_drv;      /*Unused*/

    data->state = wheel_state;
    data->enc_diff = wheel_diff;
    wheel_diff = 0;
}

/**
 * Get input from the keyboard.
 * @param indev_drv pointer to the related input device driver
 * @param data store the red data here
 */
void sdl_keyboard_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void) indev_drv;      /*Unused*/

    static bool dummy_read = false;
    const size_t len = strlen(buf);

    /*Send a release manually*/
    if (dummy_read) {
        dummy_read = false;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = len > 0;
    }
        /*Send the pressed character*/
    else if (len > 0) {
        dummy_read = true;
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = buf[0];
        memmove(buf, buf + 1, len);
        data->continue_reading = true;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

int quit_filter(void * userdata, SDL_Event * event)
{
    (void)userdata;

    if(event->type == SDL_QUIT) {
        sdl_quit_qry = true;
    }

    return 1;
}

void mouse_handler(SDL_Event * event)
{
    uint32_t win_id = UINT32_MAX;
    switch(event->type) {
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEBUTTONDOWN:
            win_id = event->button.windowID;
            break;
        case SDL_MOUSEMOTION:
            win_id = event->motion.windowID;
            break;
        case SDL_FINGERUP:
        case SDL_FINGERDOWN:
        case SDL_FINGERMOTION:
#if SDL_VERSION_ATLEAST(2,0,12)
            win_id = event->tfinger.windowID;
#endif
            break;
        case SDL_WINDOWEVENT:
            win_id = event->window.windowID;
            break;
        default:
            return;
    }

    mouse_state_t * mouse_state = NULL;

    if (win_id == monitor_1_win_id()) {
        mouse_state = &mouse_state_1;
    }
    #if SDL_DUAL_DISPLAY
    else if (win_id == monitor_2_win_id()) {
        mouse_state = &mouse_state_2;
    }
    #endif

    if (mouse_state == NULL) return;

    switch(event->type) {
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_LEFT)
                mouse_state->left_button_down = false;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event->button.button == SDL_BUTTON_LEFT) {
                mouse_state->left_button_down = true;
                mouse_state->last_x = event->motion.x / SDL_ZOOM;
                mouse_state->last_y = event->motion.y / SDL_ZOOM;
            }
            break;
        case SDL_MOUSEMOTION:
            mouse_state->last_x = event->motion.x / SDL_ZOOM;
            mouse_state->last_y = event->motion.y / SDL_ZOOM;
            break;

        case SDL_FINGERUP:
            mouse_state->left_button_down = false;
            mouse_state->last_x = SDL_HOR_RES * event->tfinger.x / SDL_ZOOM;
            mouse_state->last_y = SDL_VER_RES * event->tfinger.y / SDL_ZOOM;
            break;
        case SDL_FINGERDOWN:
            mouse_state->left_button_down = true;
            mouse_state->last_x = SDL_HOR_RES * event->tfinger.x / SDL_ZOOM;
            mouse_state->last_y = SDL_VER_RES * event->tfinger.y / SDL_ZOOM;
            break;
        case SDL_FINGERMOTION:
            mouse_state->last_x = SDL_HOR_RES * event->tfinger.x / SDL_ZOOM;
            mouse_state->last_y = SDL_VER_RES * event->tfinger.y / SDL_ZOOM;
            break;
    }
}


/**
 * It is called periodically from the SDL thread to check mouse wheel state
 * @param event describes the event
 */
void mousewheel_handler(SDL_Event * event)
{
    switch(event->type) {
        case SDL_MOUSEWHEEL:
            // Scroll down (y = -1) means positive encoder turn,
            // so invert it
#ifdef __EMSCRIPTEN__
            /*Escripten scales it wrong*/
            if(event->wheel.y < 0) wheel_diff++;
            if(event->wheel.y > 0) wheel_diff--;
#else
            wheel_diff = -event->wheel.y;
#endif
            break;
        case SDL_MOUSEBUTTONDOWN:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                wheel_state = LV_INDEV_STATE_PRESSED;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_MIDDLE) {
                wheel_state = LV_INDEV_STATE_RELEASED;
            }
            break;
        default:
            break;
    }
}


/**
 * Called periodically from the SDL thread, store text input or control characters in the buffer.
 * @param event describes the event
 */
void keyboard_handler(SDL_Event * event)
{
    /* We only care about SDL_KEYDOWN and SDL_TEXTINPUT events */
    switch(event->type) {
        case SDL_KEYDOWN:                       /*Button press*/
        {
            const uint32_t ctrl_key = keycode_to_ctrl_key(event->key.keysym.sym);
            if (ctrl_key == '\0')
                return;
            const size_t len = strlen(buf);
            if (len < KEYBOARD_BUFFER_SIZE - 1) {
                buf[len] = ctrl_key;
                buf[len + 1] = '\0';
            }
            break;
        }
        case SDL_TEXTINPUT:                     /*Text input*/
        {
            const size_t len = strlen(buf) + strlen(event->text.text);
            if (len < KEYBOARD_BUFFER_SIZE - 1)
                strcat(buf, event->text.text);
        }
            break;
        default:
            break;

    }
}


/**
 * Convert a SDL key code to it's LV_KEY_* counterpart or return '\0' if it's not a control character.
 * @param sdl_key the key code
 * @return LV_KEY_* control character or '\0'
 */
uint32_t keycode_to_ctrl_key(SDL_Keycode sdl_key)
{
    /*Remap some key to LV_KEY_... to manage groups*/
    
    SDL_Keymod mode = SDL_GetModState();
    
    switch(sdl_key) {
        case SDLK_RIGHT:
        case SDLK_KP_PLUS:
            return LV_KEY_RIGHT;

        case SDLK_LEFT:
        case SDLK_KP_MINUS:
            return LV_KEY_LEFT;

        case SDLK_UP:
            return LV_KEY_UP;

        case SDLK_DOWN:
            return LV_KEY_DOWN;

        case SDLK_ESCAPE:
            return LV_KEY_ESC;

        case SDLK_BACKSPACE:
            return LV_KEY_BACKSPACE;

        case SDLK_DELETE:
            return LV_KEY_DEL;

        case SDLK_KP_ENTER:
        case '\r':
            return LV_KEY_ENTER;

        case SDLK_TAB:
            return (mode & KMOD_SHIFT)? LV_KEY_PREV: LV_KEY_NEXT;
            
        case SDLK_PAGEDOWN:
            return LV_KEY_NEXT;

        case SDLK_PAGEUP:
            return LV_KEY_PREV;

        default:
            return '\0';
    }
}

#endif  /* USE_SDL || USD_SDL_GPU */
