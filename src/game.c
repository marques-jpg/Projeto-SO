#include "board.h"
#include "display.h"
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

// Definição de constantes de estado do jogo
#define CONTINUE_PLAY 0
#define NEXT_LEVEL 1
#define QUIT_GAME 2

// Declaração externa da função de carregamento de ficheiros (definida em board.c)
extern int load_level_filename(board_t *board, const char *filename);

void screen_refresh(board_t * game_board, int mode) {
    debug("REFRESH\n");
    draw_board(game_board, mode);
    refresh_screen();
    if(game_board->tempo != 0)
        sleep_ms(game_board->tempo);       
}

int play_board(board_t * game_board) {
    pacman_t* pacman = &game_board->pacmans[0];
    command_t* play;
    
    // Se não houver movimentos pré-definidos (n_moves == 0), usa input do utilizador
    if (pacman->n_moves == 0) { 
        command_t c; 
        c.command = get_input();

        if(c.command == '\0')
            return CONTINUE_PLAY;

        c.turns = 1;
        play = &c;
    }
    else { // Caso contrário, usa os movimentos lidos do ficheiro
        play = &pacman->moves[pacman->current_move % pacman->n_moves];
    }

    debug("KEY %c\n", play->command);

    if (play->command == 'Q') {
        return QUIT_GAME;
    }

    int result = move_pacman(game_board, 0, play);
    if (result == REACHED_PORTAL) {
        return NEXT_LEVEL;
    }

    if(result == DEAD_PACMAN) {
        return QUIT_GAME;
    }
    
    for (int i = 0; i < game_board->n_ghosts; i++) {
        ghost_t* ghost = &game_board->ghosts[i];
        move_ghost(game_board, i, &ghost->moves[ghost->current_move % ghost->n_moves]);
    }

    if (!game_board->pacmans[0].alive) {
        return QUIT_GAME;
    }      

    return CONTINUE_PLAY;  
}

// Função auxiliar para verificar a extensão de um ficheiro
int has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    return (strcmp(dot, ext) == 0);
}

// Função de comparação para o qsort (ordem alfabética)
int compare_levels(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Encontra ficheiros .lvl na diretoria e preenche a lista
int encontrar_niveis(const char *dirpath, char lista[MAX_LEVELS][MAX_FILENAME]) {
    DIR *dirp = opendir(dirpath);
    if (dirp == NULL) {
        perror("Erro ao abrir diretoria");
        return 0;
    }

    struct dirent *dp;
    int count = 0;

    while ((dp = readdir(dirp)) != NULL) {
        // Ignorar entradas "." e ".."
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        // Se for .lvl e houver espaço no array
        if (has_extension(dp->d_name, ".lvl") && count < MAX_LEVELS) {
            strncpy(lista[count], dp->d_name, MAX_FILENAME - 1);
            lista[count][MAX_FILENAME - 1] = '\0'; // Garantir null-terminator
            count++;
        }
    }
    closedir(dirp);
    
    return count;
}

int main(int argc, char** argv) {
    // Validação dos argumentos
    if (argc != 2) {
        printf("Usage: %s <level_directory>\n", argv[0]);
        return 1;
    }

    // Inicialização da seed aleatória
    srand((unsigned int)time(NULL));

    // Abre o ficheiro de debug antes de mudar de diretoria (para ficar na raiz da execução)
    open_debug_file("debug.log");

    // Muda o processo para a diretoria dos níveis
    // Isto permite que o código em board.c abra "pacman.p" diretamente sem caminhos absolutos
    if (chdir(argv[1]) != 0) {
        perror("Erro ao aceder à diretoria de níveis");
        close_debug_file();
        return 1;
    }

    // Encontra e ordena os níveis
    char lista_niveis[MAX_LEVELS][MAX_FILENAME];
    int n_niveis = encontrar_niveis(".", lista_niveis);

    if (n_niveis == 0) {
        printf("Nenhum nível (.lvl) encontrado na diretoria %s.\n", argv[1]);
        close_debug_file(); 
        return 1;
    }

    qsort(lista_niveis, n_niveis, MAX_FILENAME, compare_levels);

    terminal_init();
    
    int accumulated_points = 0;
    board_t game_board;
    // Garante que a estrutura está limpa antes de começar
    memset(&game_board, 0, sizeof(board_t));

    // Loop principal pelos níveis
    for (int i = 0; i < n_niveis; i++) {
        
        // Carrega o nível atual usando o nome do ficheiro
        if (load_level_filename(&game_board, lista_niveis[i]) != 0) {
            debug("Erro ao carregar o nível: %s\n", lista_niveis[i]);
            break; 
        }

        // Restaura os pontos acumulados (se houver Pacman)
        if (game_board.n_pacmans > 0) {
            game_board.pacmans[0].points = accumulated_points;
        }

        // Desenha o estado inicial
        draw_board(&game_board, DRAW_MENU);
        refresh_screen();

        int level_result = CONTINUE_PLAY;

        // Loop de jogo do nível atual
        while(true) {
            level_result = play_board(&game_board); 

            // Atualiza pontos acumulados para mostrar na UI
            if (game_board.n_pacmans > 0) {
                accumulated_points = game_board.pacmans[0].points;
            }

            if(level_result == NEXT_LEVEL || level_result == QUIT_GAME) {
                break;
            }
    
            screen_refresh(&game_board, DRAW_MENU); 
        }

        // Verifica o resultado do nível
        if (level_result == QUIT_GAME) {
            screen_refresh(&game_board, DRAW_GAME_OVER); 
            sleep_ms(2000); // Pausa para ver o Game Over
            unload_level(&game_board);
            break; // Sai do loop de níveis
        }
        else if (level_result == NEXT_LEVEL) {
            // Se foi o último nível, Vitória
            if (i == n_niveis - 1) {
                screen_refresh(&game_board, DRAW_WIN);
                sleep_ms(2000);
                unload_level(&game_board);
                break;
            }
            // Se não, descarrega e segue para o próximo (o loop for continua)
            unload_level(&game_board);
        }
    }    

    terminal_cleanup();
    close_debug_file();

    return 0;
}