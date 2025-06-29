#include "network.hpp"
#include "game.hpp"
#include <stdio.h>
#include <getopt.h>
#include <string>
#include <iostream>
#include <cmath>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// prefix não pode ter o path do diretório
std::string find_file_with_prefix(const std::string &dir_path, const std::string &prefix)
{
    DIR *dir = opendir(dir_path.c_str());
    if (!dir)
        throw std::runtime_error("Could not open directory: " + dir_path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        if (entry->d_name[0] == '.')
            continue; // Ignora "." e ".."
        if (strncmp(entry->d_name, prefix.c_str(), prefix.size()) == 0)
        {
            std::string found_name = entry->d_name; // Copia antes de fechar
            closedir(dir);
            return dir_path + "/" + found_name;
        }
    }
    closedir(dir);
    throw std::runtime_error("No file found with prefix: " + prefix);
}

bool all_treasures_found(Treasure_Position *treasure_positions)
{
    size_t i = 0;
    while (i < NUM_TREASURES)
    {
        if (!treasure_positions[i].found)
            return false;

        i++;
    }

    return true;
}

int main()
{
    srand((time(NULL)));

    Network net = Network();

    Map map = Map(false); // Server mode

    bool end = false;
    bool is_sending_treasure = false;
    while (!end)
    {
        map.print();
        uint8_t treasure_index;
        if (!is_sending_treasure)
        {
            Message *received_message;
            net.receive_message(received_message, false);

            switch (received_message->type)
            {
            case UP:
            case DOWN:
            case LEFT:
            case RIGHT:
            {
                map.move_player((message_type)received_message->type);

                // Testa se player encontrou um tesouro
                for (int i = 0; i < NUM_TREASURES; ++i)
                {
                    if (map.player_position == map.treasures[i].position && !(map.treasures[i].found))
                    {
                        // Marca o tesouro como encontrado
                        map.treasures[i].found = true;
                        is_sending_treasure = true;
                        treasure_index = i;
                        printf("Player found a treasure at (%d, %d)!\n", map.treasures[i].position.x, map.treasures[i].position.y);
                        break;
                    }
                }

                if (!is_sending_treasure)
                {
                    printf("Player moved. Sending ACK.\n");
                    Message ack_message = Message(0, net.my_sequence, ACK, NULL);
                    net.send_message(&ack_message);
                }

                break;
            }
            default:
            {
                printf("Received unknown message type. Sending NACK.\n");
                Message nack_message = Message(0, net.my_sequence, NACK, NULL);
                net.send_message(&nack_message);
                break;
            }
            }

            delete received_message;
            received_message = nullptr;
        }
        if (is_sending_treasure)
        {
            printf("Sending treasure to player...\n");

            // Obtém o nome do tesouro baseado no índice do tesouro encontrado
            std::string prefix = std::to_string(treasure_index + 1);

            // Pega o nome do arquivo do tesouro com base no prefixo, já que
            // a terminação é variável (pode ser .txt, .mp4 e .jpg)
            std::string name = find_file_with_prefix(TREASURE_DIR, prefix);

            // Tenta obter sufixo do arquivo
            std::size_t pos = name.find_last_of('.');
            std::string suffix;
            if (pos != std::string::npos)
            {
                suffix = name.substr(pos);
            }
            else
            {
                suffix = "";
            }

            // Testa se o arquivo é regular ou não é dos tipos esperados
            struct stat st;
            if (stat(name.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
                (suffix != ".txt" && suffix != ".mp4" && suffix != ".jpg"))
            {
                printf("Arquivo não regular ou não é do tipo esperado.\n");
                Message non_reg_ack = Message(0, net.my_sequence, NON_REGULAR_ACK, NULL);
                net.send_message(&non_reg_ack);
            }
            else
            {
                Treasure *treasure = new Treasure(name, false);

                message_type ack_type;

                if (suffix == ".txt")
                    ack_type = TXT_ACK_NAME;
                else if (suffix == ".mp4")
                    ack_type = VID_ACK_NAME;
                else if (suffix == ".jpg")
                    ack_type = IMG_ACK_NAME;

                // Size + 1 para incluir o terminador nulo
                Message ack_treasure = Message(treasure->filename.size() + 1, net.my_sequence, ack_type, treasure->filename_data);
                Message *ret = net.send_message(&ack_treasure);
                delete ret;

                // Envia mensagem com o tamanho do arquivo
                printf("ACK received. Sending treasure file size: %ld.\n", treasure->size);
                Message size_message = Message((uint8_t)8, net.my_sequence, DATA_SIZE, (uint8_t *)&treasure->size);
                Message *return_message = net.send_message(&size_message);

                // Caso em que o tamanho do tesouro é maior que o espaço livre no
                // cliente
                if (return_message && return_message->type == TOO_BIG)
                {
                    is_sending_treasure = false;

                    // Envia ack
                    Message ack_message = Message(0, net.my_sequence, ACK, NULL);
                    net.send_message(&ack_message);

                    // Checa se todos os tesouros foram encontrados

                    if (all_treasures_found(map.treasures))
                    {
                        puts("All treasures found! Ending game.");
                        end = true;
                    }

                    delete treasure;
                    continue;
                }

                // Envia o arquivo do tesouro
                puts("ACK received. Sending treasure file data...");

                size_t j = 0;
                uint64_t bytes_extras = 0;
                uint64_t buffer_size = 0;
                // Calcula o tamanho do buffer necessário para armazenar os
                // dados do tesouro, incluindo os bytes de stuffing
                for (size_t i = 0; i < treasure->size; i++)
                {
                    if (treasure->data[i] == FORBIDDEN_BYTE_1 || treasure->data[i] == FORBIDDEN_BYTE_2)
                    {
                        if ((j % MAX_DATA_SIZE) == (MAX_DATA_SIZE - 1))
                            bytes_extras++;

                        j++;
                    }
                    j++;
                }

                buffer_size = j;
                uint8_t *buffer = new uint8_t[buffer_size];

                if (!buffer)
                {
                    fprintf(stderr, "Memory allocation failed for buffer.\n");
                    Message too_big_message = Message(0, net.my_sequence, TOO_BIG, NULL);
                    Message *r = net.send_message(&too_big_message);
                    // A mensagem de retorno r é inútil, porém temos que
                    // desalocá-la para evitar leak
                    delete r;
                    delete treasure;
                }

                j = 0;

                // Copia treasure->data para buffer e adiciona stuffying bytes
                for (size_t i = 0; i < treasure->size; i++)
                {
                    buffer[j] = treasure->data[i];

                    if (treasure->data[i] == FORBIDDEN_BYTE_1 || treasure->data[i] == FORBIDDEN_BYTE_2)
                    {
                        if ((j % MAX_DATA_SIZE) == (MAX_DATA_SIZE - 1))
                            bytes_extras++;

                        j++;
                        buffer[j] = STUFFING_BYTE;
                    }
                    j++;
                }

                uint8_t *data_chunk = new uint8_t[MAX_DATA_SIZE];
                uint32_t num_messages = std::ceil((double)(buffer_size + bytes_extras) / MAX_DATA_SIZE);
                size_t start_byte = 0;

                for (size_t i = 0; i < num_messages; i++)
                {
                    uint8_t chunk_size = std::min((size_t)MAX_DATA_SIZE, (size_t)(buffer_size - start_byte));

                    // Exibe progresso do envio
                    if (i % 50 == 0) // Atualiza a cada 20 mensagens
                        printf("Sending... %.1f%%", ((float)i / (float)num_messages) * 100);

                    // Vê se o último byte é proibido
                    if (chunk_size == MAX_DATA_SIZE &&
                        (buffer[start_byte + chunk_size - 1] == FORBIDDEN_BYTE_1 ||
                         buffer[start_byte + chunk_size - 1] == FORBIDDEN_BYTE_2))
                    {
                        // Deixa o byte proibido para a próxima mensagem
                        chunk_size--;
                    }

                    memcpy(data_chunk, buffer + start_byte, chunk_size);

                    Message treasure_message = Message(chunk_size, net.my_sequence, DATA, data_chunk);
                    Message *r = net.send_message(&treasure_message);
                    // A mensagem de retorno r é inútil, porém temos que
                    // desalocá-la para evitar leak
                    delete r;

                    // Apaga última linha de carregamento
                    if (i % 50 == 0) // Atualiza a cada 20 mensagens
                        printf("\r\033[2K");

                    // Atualiza o próximo início de mensagem
                    start_byte += chunk_size;
                }

                // Envia mensagem de fim de transmissão
                Message end_message = Message(0, net.my_sequence, END, NULL);
                Message *r = net.send_message(&end_message);
                // A mensagem de retorno r é inútil, porém temos que
                // desalocá-la para evitar leak
                delete r;

                delete[] data_chunk;
                delete[] buffer;
                delete treasure;
            }

            is_sending_treasure = false;

            // Checa se todos os tesouros foram encontrados
            if (all_treasures_found(map.treasures))
            {
                puts("All treasures found! Ending game.");
                end = true;
            }
        }
    }
    return 0;
}
