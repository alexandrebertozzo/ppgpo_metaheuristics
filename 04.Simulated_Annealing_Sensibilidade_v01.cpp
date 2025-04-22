//04.Simulated_Annealing_Sensibilidade_v01.cpp

// =======================
// DEFINICOES DO PCG: Gerador de números aleatórios mais sofisticado
// =======================
#define PCG_ENABLE_INLINE_ASM 0
#define PCG_FORCE_PURE_C 0
#define PCG_LITTLE_ENDIAN 1

// =======================
// INCLUIR HEADERS DO PCG PRIMEIRO
// =======================
#include "pcg_random.hpp"
#include "pcg_extras.hpp"
#include "pcg_uint128.hpp"

// =======================
// DEMAIS INCLUDES
// =======================
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <climits>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <tuple>
#include <numeric>
#include <random>  // pode permanecer, se usado com uniform distributions


using namespace std;


// Estrutura para armazenar os tipos de bin (capacidade e custo)
struct BinTipo {
    int capacidade;
    int custo;
};

// Estrutura para armazenar os itens (peso)
struct Item {
    int peso;
};

// Estrutura para armazenar uma instancia do problema
struct Instancia {
    int n = 0;                            // numero de itens
    int m = 0;                            // numero de tipos de bin
    vector<BinTipo> tipos;                // tipos de bin disponiveis
    vector<Item> itens;                   // lista de itens
};

// Estrutura para representar um bin usado na solucao
struct BinUsado {
    int tipo = 0;                         // tipo do bin (indice no vetor tipos da instancia)
    vector<int> itens;                    // indices dos itens alocados neste bin
};

// Operador de igualdade para permitir comparações entre bins
bool operator==(const BinUsado& a, const BinUsado& b) {
    return a.tipo == b.tipo && a.itens == b.itens;
}


// Estrutura para representar uma solucao do problema
struct Solucao {
    vector<BinUsado> bins;                // lista de bins usados
    int custo_total = 0;                  // custo total da solucao (calculado ao final da geracao)
    double tempo_execucao_ms = 0.0;       // tempo de execucao
};


/**
 * Comentarios no estilo Doxygen
 *
 * @brief: Lê uma instancia do problema VSBPP a partir de um arquivo texto.
 *
 * Esta funcao abre o arquivo especificado, lê o numero de itens e tipos de bin,
 * seguido pelas capacidades e custos dos bins e, por fim, os pesos dos itens.
 * Todos os dados lidos sao armazenados na estrutura Instancia fornecida.
 *
 *    @param: txtInstancia_corrente Nome do arquivo txt de entrada.
 *    @param: instancia_corrente Referencia para a struct Instancia a ser preenchida.
 *    @return: true se a leitura for bem-sucedida; false em caso de erro na abertura do arquivo.
 */
bool lerInstancia(const string& txtInstancia_corrente, Instancia& instancia_corrente)
{

    // Abre o arquivo para leitura
    ifstream arquivo(txtInstancia_corrente);

    // Verifica se o arquivo foi aberto com sucesso
    if (!arquivo.is_open())
    {
        // O operador '<<' manda a mensagem abaixo (seguida por quebra de linha) para a saida padrao de erro (cerr)
        cerr << "Erro ao abrir arquivo: " << txtInstancia_corrente << endl;
        return false;
    }

    // Lê o numero de itens (n) e o numero de tipos de bin (m)
    // O operador '>>' lê proximo valor inteiro, ignorando espaços/tabulacoes/quebras de linha
    // Ignora os dois proximos valores (que nao fazem parte da instancia)
    int lixo1, lixo2;
    arquivo >> instancia_corrente.n >> instancia_corrente.m >> lixo1 >> lixo2;

    // Redimensiona o vetor de tipos de bin para armazenar m elementos
    instancia_corrente.tipos.resize(instancia_corrente.m);

    // Lê as capacidades e os custos dos m tipos de bin
    for (int i = 0; i < instancia_corrente.m; i++)
    {
        // operador >> duas vezes para ler dois valores inteiros seguidos do arquivo
        // e os armazena respectivamente em capacidade e custo do tipo de bin na posicao i
        arquivo >> instancia_corrente.tipos[i].capacidade
            >> instancia_corrente.tipos[i].custo;
    }

    // Redimensiona o vetor de itens para armazenar n elementos
    instancia_corrente.itens.resize(instancia_corrente.n);

    // Lê os pesos dos n itens
    for (int i = 0; i < instancia_corrente.n; i++)
    {
        // O operador '>>' lê o proximo valor inteiro do arquivo
        // e o armazena como peso do item na posicao i do vetor de itens
        arquivo >> instancia_corrente.itens[i].peso;
    }

    // Fecha o arquivo apos a leitura
    arquivo.close();

    // Indica que a leitura foi bem-sucedida
    return true;
}



/**
 * @brief:  Imprime os dados de uma instancia do problema VSBPP.
 *
 * Exibe o numero de itens, o numero de tipos de bin, as capacidades e custos
 * de cada tipo de bin, e os pesos de todos os itens.
 *
 *    @param: instancia_corrente Struct Instancia preenchida previamente com os dados do arquivo.
 */
void imprimirInstancia(string nomeInstancia_corrente, const Instancia& instancia_corrente)
{
    // Imprime cabecalho geral da instancia  << txtInstancia_corrente << endl
    cout << "---------- INSTANCIA: " << nomeInstancia_corrente << " ----------" << endl;
    cout << "Numero de itens (n): " << instancia_corrente.n << endl;
    cout << "Numero de tipos de bin (m): " << instancia_corrente.m << endl;
    cout << endl;

    // Imprime os tipos de bin (capacidade e custo)
    cout << "Tipos de bin (capacidade / custo):" << endl;
    for (int i = 0; i < instancia_corrente.m; i++) {
        cout << "  Tipo " << (i + 1) << ": "
            << instancia_corrente.tipos[i].capacidade << " / "
            << instancia_corrente.tipos[i].custo << endl;
    }

    cout << endl;

    // Imprime os pesos dos itens
    cout << "Pesos dos itens:" << endl;
    for (int i = 0; i < instancia_corrente.n; i++) {
        cout << instancia_corrente.itens[i].peso << " ";

        // Quebra linha a cada 20 itens para melhor visualizacao
        if ((i + 1) % 20 == 0)
            cout << endl;
    }

    cout << "\n-------------------------------" << endl << endl << endl;
}


/**
 * @brief: Calcula o custo total da solucao
 *
 * Soma os custos dos tipos de bin utilizados na solucao corrente
 *
 *    @param: solucao Solucao atual
 *    @param: instancia Dados da instancia (inclui custos dos tipos de bin)
 *    @return: Custo total acumulado
 */
int calcularCustoTotal(const Solucao& solucao, const Instancia& instancia) {
    int custo_total = 0;

    // Soma os custos de cada bin utilizado na solucao
    for (const BinUsado& bin : solucao.bins) {
        int tipo = bin.tipo;
        custo_total += instancia.tipos[tipo].custo;
    }

    return custo_total;
}

/**
 * @brief: Gera uma solucao aleatoria viavel para a instancia do problema.
 *
 * Esta funcao distribui os itens aleatoriamente entre os bins respeitando a capacidade
 * maxima de cada bin, de acordo com o tipo sorteado. Cada item e atribuido a apenas um bin,
 * e cada bin recebe um tipo aleatorio dentre os disponiveis na instancia.
 *
 *    @param:  instancia_corrente Estrutura com os dados da instancia atual.
 *    @return: Estrutura Solucao contendo bins com os itens alocados de forma viavel.
 */
Solucao gerarSolucaoAleatoriaViavel(const Instancia& instancia_corrente, pcg32& rng)
{
    using namespace std::chrono;
    auto inicio = high_resolution_clock::now();

    // Estrutura que armazenara a solucao
    Solucao solucao_corrente;

    // Cria um vetor vazio que armazenara os indices dos itens ainda nao foram alocados
    vector<int> itens_restantes;

    // Preenche o vetor com os valores de 0 ate n - 1 (ou seja, todos os itens)
    for (int i = 0; i < instancia_corrente.n; ++i) {
        itens_restantes.push_back(i);  // Adiciona o indice do item i ao vetor
    }

    // Enquanto houver itens nao alocados
    while (!itens_restantes.empty()) {
        // Cria um novo bin
        BinUsado novo_bin;

        // Sorteia um tipo de bin aleatorio (entre 0 e m - 1)
        uniform_int_distribution<int> dist(0, instancia_corrente.m - 1);  // distribui de 0 a m-1
        novo_bin.tipo = dist(rng); // sorteia tipo do bin

        // Capacidade maxima do tipo sorteado
        int capacidade_disponivel = instancia_corrente.tipos[novo_bin.tipo].capacidade;

        // Embaralha os itens restantes para alocar em ordem aleatoria
        shuffle(itens_restantes.begin(), itens_restantes.end(), rng);  // embaralha usando gerador externo

        // Lista temporaria para armazenar os itens que nao couberam neste bin
        vector<int> itens_nao_alocados;

        // Tenta alocar cada item no bin atual respeitando a capacidade
        for (size_t i = 0; i < itens_restantes.size(); ++i) {
            int idx = itens_restantes[i]; // Indice do item
            int peso_item = instancia_corrente.itens[idx].peso; // Peso do item

            if (peso_item <= capacidade_disponivel) {
                novo_bin.itens.push_back(idx); // Aloca item no bin
                capacidade_disponivel -= peso_item; // Atualiza capacidade restante do bin
            }
            else {
                itens_nao_alocados.push_back(idx); // Item nao coube, mantem para a proxima rodada
            }
        }

        // Atualiza a lista de itens restantes com os que ainda nao foram alocados
        itens_restantes = itens_nao_alocados;

        // Apenas adiciona o novo bin na solucao se o mesmo nao estiver vazio
        if (!novo_bin.itens.empty()) {
            solucao_corrente.bins.push_back(novo_bin);
        }
    }
    // Custo total da solução
    solucao_corrente.custo_total = calcularCustoTotal(solucao_corrente, instancia_corrente);

    // Tempo total da solução
    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;
    solucao_corrente.tempo_execucao_ms = duracao.count();

    // Retorna a solucao viavel gerada
    return solucao_corrente;
}

/**
 * @brief: Gera uma solucao gulosa adaptada do First Fit Decreasing para VSBPP.
 *
 * Para cada item (em ordem decrescente de peso), tenta alocar no primeiro bin ja aberto com espaco suficiente.
 * Se nao couber em nenhum, abre um novo bin do tipo mais barato que comporte o item.
 *
 * @param instancia_corrente Estrutura com os dados da instancia atual.
 * @return Solucao preenchida de forma gulosa.
 */
Solucao gerarSolucaoGulosaFFD(const Instancia& instancia_corrente)
{
    using namespace std::chrono;
    auto inicio = high_resolution_clock::now();

    // Estrutura que armazenara a solucao
    Solucao solucao_corrente;

    // Cria vetor com indices dos itens: [0, 1, ..., n-1]
    vector<int> indices_itens(instancia_corrente.n);
    for (int i = 0; i < instancia_corrente.n; ++i)
        indices_itens[i] = i;

    // Ordena os itens por peso decrescente
    sort(indices_itens.begin(), indices_itens.end(),
        [&instancia_corrente](int a, int b) {
            return instancia_corrente.itens[a].peso > instancia_corrente.itens[b].peso;
        });

    // Vetor que armazena a capacidade restante de cada bin ja aberto
    vector<int> capacidades_restantes;

    // Para cada item, seguindo a ordem decrescente de peso
    for (int item_idx : indices_itens) {
        int peso_item = instancia_corrente.itens[item_idx].peso; // Peso do item atual
        bool alocado = false; // Flag para saber se o item foi alocado

        // Tenta alocar no primeiro bin ja aberto com espaco suficiente
        for (size_t i = 0; i < solucao_corrente.bins.size(); ++i) {
            if (capacidades_restantes[i] >= peso_item) {
                solucao_corrente.bins[i].itens.push_back(item_idx); // Aloca item no bin
                capacidades_restantes[i] -= peso_item;     // Atualiza capacidade restante do bin
                alocado = true;                            // Marca como alocado
                break;                                     // Encerra busca de bin
            }
        }

        // Se item nao foi alocado, precisa abrir novo bin
        if (!alocado) {
            int melhor_tipo = -1;      // Tipo de bin mais barato viavel
            int menor_custo = INT_MAX; // Inicializa com custo muito alto

            // Busca o tipo de bin mais barato que suporte o item
            for (int t = 0; t < instancia_corrente.m; ++t) {
                int capacidade = instancia_corrente.tipos[t].capacidade;
                int custo = instancia_corrente.tipos[t].custo;

                if (capacidade >= peso_item && custo < menor_custo) {
                    melhor_tipo = t;       // Armazena tipo atual como melhor
                    menor_custo = custo;   // Atualiza menor custo
                }
            }

            // Se nenhum tipo viavel for encontrado, exibe erro e encerra
            if (melhor_tipo == -1) {
                cerr << "Erro: item " << item_idx << " com peso " << peso_item
                    << " nao cabe em nenhum tipo de bin!" << endl;
                exit(EXIT_FAILURE);
            }

            // Cria novo bin com o tipo mais barato encontrado
            BinUsado novo_bin;
            novo_bin.tipo = melhor_tipo;           // Define tipo do novo bin
            novo_bin.itens.push_back(item_idx);    // Aloca o item no novo bin

            // Adiciona o novo bin na solucao_corrente
            solucao_corrente.bins.push_back(novo_bin);

            // Calcula capacidade restante apos alocar o item
            int capacidade_restante = instancia_corrente.tipos[melhor_tipo].capacidade - peso_item;
            capacidades_restantes.push_back(capacidade_restante);
        }
    }

    // Custo total da solução
    solucao_corrente.custo_total = calcularCustoTotal(solucao_corrente, instancia_corrente);

    // Tempo total da solução
    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;
    solucao_corrente.tempo_execucao_ms = duracao.count();

    // Retorna a solucao_corrente gulosa gerada
    return solucao_corrente;
}


/**
 * @brief: Funcao central de construcao da solucao, que escolhe a estrategia de acordo com o parametro.
 *
 * Com base no nome do metodo informado, esta funcao redireciona a execucao para a
 * estrategia correspondente. Isso permite flexibilidade para adicionar novos
 * metodos de construcao no futuro (ex: gulosa, construtiva, aleatoria com restricoes, etc).
 *
 *    @param:  instancia_corrente Estrutura com os dados da instancia atual.
 *    @param:  metodo Nome do metodo de construcao ("aleatoria", "gulosa", etc).
 *    @return: Estrutura Solucao preenchida conforme a estrategia escolhida.
 */
Solucao construirSolucao(const Instancia& instancia_corrente, const string& metodo, pcg32& rng)
{
    // Caso a estrategia escolhida seja "aleatoria" que respeita capacidade dos bins
    if (metodo == "aleatoria_viavel") {
        return gerarSolucaoAleatoriaViavel(instancia_corrente, rng);
    }

    // Caso a estrategia escolhida seja "gulosa"
    else if (metodo == "gulosa_FFD") {
        return gerarSolucaoGulosaFFD(instancia_corrente);
    }

    // Se o nome do metodo for desconhecido, exibe erro e encerra o programa
    else {
        cerr << "Metodo de construcao desconhecido: " << metodo << endl;
        exit(EXIT_FAILURE); // encerra com erro
    }
}

/**
 * @brief Imprime a solucao de forma resumida ou detalhada, de acordo com o flag.
 *
 * Se o parametro imprimir_bins for true, exibe para cada bin:
 * - O numero do bin (comecando de 1)
 * - O tipo de bin (comecando de 1)
 * - A capacidade do bin e o total de peso alocado
 * - Os itens alocados com seus indices (formato humano) e pesos
 *
 * Se imprimir_bins for false, imprime apenas o custo total e tempo de execucao.
 *
 * @param solucao_corrente Estrutura contendo os bins usados e os itens alocados
 * @param instancia_corrente Dados da instancia lida, usados para consultar capacidades e pesos
 * @param metodo Nome da estrategia utilizada para gerar a solucao (ex: "aleatoria_viavel")
 * @param imprimir_bins Flag de controle (true para imprimir todos os bins, false para modo compacto)
 */
void imprimirSolucao(const Solucao& solucao_corrente,
    const Instancia& instancia_corrente,
    const string& metodo,
    bool imprimir_bins)
{
    // Imprime o cabecalho principal da solucao
    cout << "---------- SOLUCAO: " << metodo << " ----------" << endl;

    // Se o flag estiver ativo, imprime os bins detalhadamente
    if (imprimir_bins) {
        // Cabecalho da tabela
        cout << "Bins\t\tTipos (capacidade / peso itens)\t\tItens (peso)" << endl;

        // Para cada bin da solucao
        for (size_t i = 0; i < solucao_corrente.bins.size(); ++i)
        {
            const BinUsado& bin = solucao_corrente.bins[i];       // Referencia ao bin atual
            int tipo_bin = bin.tipo;                               // Tipo de bin (indice no vetor de tipos)
            int capacidade = instancia_corrente.tipos[tipo_bin].capacidade; // Capacidade do tipo

            // Calcula o peso total dos itens neste bin
            int total_utilizado = 0;
            for (int item_idx : bin.itens) {
                total_utilizado += instancia_corrente.itens[item_idx].peso;
            }

            // Imprime numero do bin e tipo com capacidade/peso total
            cout << "BIN " << (i + 1) << "\t\t"
                << (tipo_bin + 1) << " (" << capacidade << " / " << total_utilizado << ")\t\t\t\t";

            // Imprime os itens alocados (indice humano e peso)
            for (size_t j = 0; j < bin.itens.size(); ++j) {
                int item_idx = bin.itens[j];
                int peso = instancia_corrente.itens[item_idx].peso;
                cout << (item_idx + 1) << " (" << peso << ")";
                if (j < bin.itens.size() - 1)
                    cout << ", ";
            }

            // Quebra de linha após cada bin
            cout << endl;
        }
    }

    // Imprime sempre o custo total da solucao
    cout << endl << "Custo total da solucao: " << solucao_corrente.custo_total << endl;

    // Imprime o tempo de execucao com 3 casas decimais
    cout << fixed << setprecision(3);
    cout << "Tempo de execucao (ms): " << solucao_corrente.tempo_execucao_ms << endl;

    // Rodapé delimitador
    cout << "-----------------------------" << endl << endl << endl;
}


/**
 * @brief Calcula a soma dos pesos de itens em um bin.
 *
 * @param bin Bin a ser avaliado.
 * @param instancia Dados da instancia contendo os pesos.
 * @return Soma total dos pesos dos itens alocados no bin.
 */
int calcularCarga(const BinUsado& bin, const Instancia& instancia) {
    int carga = 0;
    for (int id : bin.itens) {
        carga += instancia.itens[id].peso; // soma os pesos de todos os itens no bin
    }
    return carga;
}

/**
 * @brief Calcula a taxa media de ocupacao dos bins da solucao.
 *
 * A taxa de ocupacao e definida como carga/capacidade. Esta funcao retorna
 * a media dessas taxas entre todos os bins da solucao.
 *
 * @param sol Solucao atual.
 * @param instancia Dados da instancia.
 * @return Taxa media de ocupacao (entre 0 e 1).
 */
double calcularTaxaOcupacao(const Solucao& sol, const Instancia& instancia) {
    double soma = 0.0;
    for (const auto& bin : sol.bins) {
        int carga = calcularCarga(bin, instancia); // calcula a carga do bin
        soma += static_cast<double>(carga) / instancia.tipos[bin.tipo].capacidade; // soma a taxa do bin
    }
    return soma / sol.bins.size(); // retorna a media das taxas
}

/**
 * @brief Gera vizinhos viaveis a partir de um modo sorteado (1 a 5), limitado por max_vizinhos.
 *
 * Modo 1: troca para qualquer tipo que suporte a carga
 * Modo 2: troca de itens entre bins diferentes
 * Modo 3: troca para tipo mais barato viavel
 * Modo 4: remove bin se seus itens puderem ser realocados
 * Modo 5: troca para tipo mais caro viavel
 *
 * @param solucao Solucao atual.
 * @param instancia Dados da instancia.
 * @param rng Gerador aleatorio.
 * @param max_vizinhos Limite maximo de vizinhos a serem gerados.
 * @return Vetor com ate max_vizinhos vizinhos gerados.
 */
vector<Solucao> gerarVizinhos(const Solucao& solucao, const Instancia& instancia, pcg32& rng, int max_vizinhos, int modo)
{
    vector<Solucao> vizinhos;

    if (modo == 1) {
        // Modo 1: troca para tipo qualquer viavel
        // Aplica o modo atual e acumula os vizinhos gerados (limitados por MAX_VIZINHOS / 5)
        for (size_t i = 0; i < solucao.bins.size(); ++i) {
            int carga = calcularCarga(solucao.bins[i], instancia);
            for (int t = 0; t < instancia.m; ++t) {
                if (t != solucao.bins[i].tipo && instancia.tipos[t].capacidade >= carga) {
                    Solucao v = solucao;
                    v.bins[i].tipo = t;
                    vizinhos.push_back(v);
                    if (vizinhos.size() >= max_vizinhos) return vizinhos; // interrompe se atingir o limite
                }
            }
        }
    }

    else if (modo == 2) {
        // Modo 2: troca de itens entre bins
        // Aplica o modo atual e acumula os vizinhos gerados (limitados por MAX_VIZINHOS / 5)
        for (size_t a = 0; a < solucao.bins.size(); ++a) {
            for (size_t b = a + 1; b < solucao.bins.size(); ++b) {
                const BinUsado& binA = solucao.bins[a];
                const BinUsado& binB = solucao.bins[b];
                for (int i : binA.itens) {
                    for (int j : binB.itens) {
                        int pa = instancia.itens[i].peso;
                        int pb = instancia.itens[j].peso;
                        int ca = instancia.tipos[binA.tipo].capacidade;
                        int cb = instancia.tipos[binB.tipo].capacidade;
                        int sa = calcularCarga(binA, instancia);
                        int sb = calcularCarga(binB, instancia);
                        if (sa - pa + pb <= ca && sb - pb + pa <= cb) {
                            Solucao v = solucao;
                            auto& va = v.bins[a].itens;
                            auto& vb = v.bins[b].itens;
                            replace(va.begin(), va.end(), i, j);
                            replace(vb.begin(), vb.end(), j, i);
                            vizinhos.push_back(v);
                            if (vizinhos.size() >= max_vizinhos) return vizinhos;
                        }
                    }
                }
            }
        }
    }

    else if (modo == 3) {
        // Modo 3: troca para tipo mais barato viavel
        // Aplica o modo atual e acumula os vizinhos gerados (limitados por MAX_VIZINHOS / 5)
        for (size_t i = 0; i < solucao.bins.size(); ++i) {
            int carga = calcularCarga(solucao.bins[i], instancia);
            int tipo_atual = solucao.bins[i].tipo;
            int custo_min = instancia.tipos[tipo_atual].custo;
            int melhor_tipo = tipo_atual;

            for (int t = 0; t < instancia.m; ++t) {
                if (t != tipo_atual && instancia.tipos[t].capacidade >= carga && instancia.tipos[t].custo < custo_min) {
                    melhor_tipo = t;
                    custo_min = instancia.tipos[t].custo;
                }
            }

            if (melhor_tipo != tipo_atual) {
                Solucao v = solucao;
                v.bins[i].tipo = melhor_tipo;
                vizinhos.push_back(v);
                if (vizinhos.size() >= max_vizinhos) return vizinhos;
            }
        }
    }

    else if (modo == 4) {
        // Modo 4: remover bin com realocacao viavel dos itens
        // Aplica o modo atual e acumula os vizinhos gerados (limitados por MAX_VIZINHOS / 5)
        for (size_t i = 0; i < solucao.bins.size(); ++i) {
            const BinUsado& bin_remover = solucao.bins[i];
            vector<int> itens_a_realocar = bin_remover.itens;

            for (int t = 0; t < instancia.m; ++t) {
                Solucao v = solucao;
                v.bins.erase(v.bins.begin() + i); // remove bin
                bool sucesso = true;

                for (int item_idx : itens_a_realocar) {
                    bool alocado = false;
                    for (auto& bin : v.bins) {
                        int carga = calcularCarga(bin, instancia);
                        if (instancia.tipos[bin.tipo].capacidade - carga >= instancia.itens[item_idx].peso) {
                            bin.itens.push_back(item_idx);
                            alocado = true;
                            break;
                        }
                    }

                    if (!alocado) {
                        sucesso = false;
                        break;
                    }
                }

                if (sucesso) {
                    vizinhos.push_back(v);
                    if (vizinhos.size() >= max_vizinhos) return vizinhos;
                }
            }
        }
    }

    else if (modo == 5) {
        // Modo 5: troca para tipo mais caro viavel
        // Aplica o modo atual e acumula os vizinhos gerados (limitados por MAX_VIZINHOS / 5)
        for (size_t i = 0; i < solucao.bins.size(); ++i) {
            int carga = calcularCarga(solucao.bins[i], instancia);
            int tipo_atual = solucao.bins[i].tipo;
            int custo_max = instancia.tipos[tipo_atual].custo;
            int pior_tipo = tipo_atual;

            for (int t = 0; t < instancia.m; ++t) {
                if (t != tipo_atual && instancia.tipos[t].capacidade >= carga && instancia.tipos[t].custo > custo_max) {
                    pior_tipo = t;
                    custo_max = instancia.tipos[t].custo;
                }
            }

            if (pior_tipo != tipo_atual) {
                Solucao v = solucao;
                v.bins[i].tipo = pior_tipo;
                vizinhos.push_back(v);
                if (vizinhos.size() >= max_vizinhos) return vizinhos;
            }
        }
    }

    return vizinhos;
}

/**
 * @brief Executa busca local Descent testando vizinhos de todos os modos por iteracao,
 *        respeitando um tempo limite de execucao em milissegundos.
 *
 * Em cada iteracao, os cinco modos de vizinhanca (1 a 5) sao aplicados.
 * Cada modo gera no maximo max_vizinhos / 5 vizinhos.
 * A busca e interrompida se o tempo total ultrapassar tempo_limite_ms.
 *
 * @param instancia Dados da instancia.
 * @param solucao_inicial Solucao inicial.
 * @param rng Gerador aleatorio.
 * @param max_vizinhos Limite total de vizinhos por iteracao (dividido entre os modos).
 * @param tempo_limite_ms Tempo maximo de execucao em milissegundos.
 * @return Melhor solucao encontrada dentro do tempo limite.
 */
Solucao executarDescent(const Instancia& instancia,
    Solucao solucao_inicial,
    pcg32& rng,
    int max_vizinhos,
    int tempo_limite_ms)
{
    using namespace chrono;

    // Marca o inicio da execucao da busca local
    auto inicio = high_resolution_clock::now();

    // Inicializa a melhor solucao com a solucao inicial recebida
    Solucao melhor = solucao_inicial;
    int melhor_custo = calcularCustoTotal(melhor, instancia);
    bool melhorou = true;

    // Loop principal da busca local
    while (melhorou) {
        melhorou = false;

        // Verifica se o tempo decorrido ultrapassou o tempo limite
        auto agora = high_resolution_clock::now();
        duration<double, milli> tempo_decorrido = agora - inicio;
        if (tempo_decorrido.count() > tempo_limite_ms) {
            break; // interrompe a busca se exceder o tempo limite
        }

        // Gera vizinhos combinando todos os modos
        vector<Solucao> vizinhos;
        for (int modo = 1; modo <= 5; ++modo) {
            vector<Solucao> vizinhos_parciais = gerarVizinhos(melhor, instancia, rng, max_vizinhos / 5, modo);
            vizinhos.insert(vizinhos.end(), vizinhos_parciais.begin(), vizinhos_parciais.end());
        }

        // Avalia cada vizinho gerado
        for (const auto& vizinho : vizinhos) {
            int custo_viz = calcularCustoTotal(vizinho, instancia);

            // Se o vizinho for melhor e diferente da solucao atual
            if (custo_viz < melhor_custo && vizinho.bins != melhor.bins) {
                melhor = vizinho;
                melhor_custo = custo_viz;
                melhorou = true;
            }
        }
    }

    // Marca o fim da execucao
    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;

    // Atualiza tempo total de execucao acumulado
    melhor.custo_total = melhor_custo;
    melhor.tempo_execucao_ms = solucao_inicial.tempo_execucao_ms + duracao.count();

    return melhor; // retorna a melhor solucao encontrada
}

/**
 * @brief Executa o algoritmo Simulated Annealing (SA) para o problema VSBPP.
 *
 * Em cada temperatura, o algoritmo realiza SAmax iteracoes, gerando vizinhos aleatorios de um dos cinco modos.
 * Se a nova solucao for melhor, ela e aceita. Caso contrario, pode ser aceita com uma probabilidade
 * decrescente conforme a diferenca de custo e a temperatura. A temperatura e reduzida multiplicativamente (T <- alpha * T).
 *
 * Dois criterios de parada sao suportados:
 * - Tempo limite em milissegundos (tempo_limite_ms)
 * - Limiar adaptativo de ciclos sem melhora (ativado via usar_limiar_adaptativo)
 *
 * Caso o ponteiro log_convergencia esteja ativo, a funcao grava a evolucao da temperatura e do melhor custo no arquivo CSV.
 *
 * @param instancia Dados da instancia.
 * @param solucao_inicial Solucao inicial.
 * @param rng Gerador aleatorio.
 * @param SAmax Numero de iteracoes por temperatura.
 * @param T0 Temperatura inicial.
 * @param alpha Fator de resfriamento (entre 0 e 1).
 * @param tempo_limite_ms Tempo maximo de execucao em milissegundos.
 * @param log_convergencia Ponteiro para arquivo CSV de log (pode ser nullptr).
 * @param vizinhos_aceitos Contador de vizinhos aceitos.
 * @param vizinhos_rejeitados Contador de vizinhos rejeitados.
 * @param solucoes_piores_aceitas Contador de solucoes piores aceitas.
 * @param usar_limiar_adaptativo Se true, utiliza criterio de parada por ciclos sem melhora.
 * @param max_sem_melhora Numero maximo de ciclos consecutivos sem melhora permitidos.
 * @return Melhor solucao encontrada durante a execucao.
 * - A comparacao de melhoria entre ciclos considera uma tolerancia de 0.5% (melhorias menores sao ignoradas)
 */
Solucao executarSimulatedAnnealing(const Instancia& instancia,
    Solucao solucao_inicial,
    pcg32& rng,
    int SAmax,
    double T0,
    double alpha,
    int tempo_limite_ms,
    ofstream* log_convergencia,
    int& vizinhos_aceitos,
    int& vizinhos_rejeitados,
    int& solucoes_piores_aceitas,
    bool usar_limiar_adaptativo,
    int max_sem_melhora)
{
    using namespace chrono;
    auto inicio = high_resolution_clock::now();

    Solucao s = solucao_inicial;
    int custo_s = calcularCustoTotal(s, instancia);
    Solucao melhor = s;
    int melhor_custo = custo_s;

    double T = T0;
    int IterT = 0;

    uniform_real_distribution<double> prob_dist(0.0, 1.0);

    int iter_total = 0;           // total de iteracoes acumuladas
    int ciclos_sem_melhoria = 0;  // contador de ciclos consecutivos sem melhoria
    int melhor_custo_ultima_verificacao = melhor_custo;


    while (true) {
        auto agora = high_resolution_clock::now();
        duration<double, milli> tempo_decorrido = agora - inicio;

        // Se usar parada adaptativa, verifica ciclos sem melhora
        if (usar_limiar_adaptativo) {
            if (ciclos_sem_melhoria >= max_sem_melhora)
                break;
        }
        // Caso contrario, aplica limite de tempo
        else {
            if (tempo_decorrido.count() > tempo_limite_ms)
                break;
        }


        IterT = 0;

        while (IterT < SAmax) {
            IterT++;

            // Escolhe modo de vizinhanca aleatoriamente (1 a 5)
            // Isso permite explorar todas as estrategias de vizinhanca definidas
            uniform_int_distribution<int> modo_dist(1, 5);
            int modo = modo_dist(rng);

            // Gera um vizinho aleatorio (apenas 1)
            vector<Solucao> vizinhos = gerarVizinhos(s, instancia, rng, 1, modo);

            if (vizinhos.empty())
                continue;

            Solucao s_linha = vizinhos[0];
            int custo_s_linha = calcularCustoTotal(s_linha, instancia);
            int delta = custo_s_linha - custo_s;

            iter_total++; // incremento acumulado de iteracoes

            if (delta <= 0) {
                s = s_linha;
                custo_s = custo_s_linha;
                vizinhos_aceitos++;
                if (custo_s_linha < melhor_custo) {
                    melhor = s_linha;
                    melhor_custo = custo_s_linha;
                }
            }
            else {
                double p = exp(-static_cast<double>(delta) / T);
                if (prob_dist(rng) < p) {
                    s = s_linha;
                    custo_s = custo_s_linha;
                    solucoes_piores_aceitas++;
                    vizinhos_aceitos++;
                }
                else {
                    vizinhos_rejeitados++;
                }
            }


            auto tempo_atual = high_resolution_clock::now();
            duration<double, milli> tempo_total = tempo_atual - inicio;

            // Aplica limite de tempo apenas se nao for usar parada adaptativa
            if (!usar_limiar_adaptativo && tempo_total.count() > tempo_limite_ms)
                break;

        }

        // Registro da convergencia com colunas adicionais
        if (log_convergencia && log_convergencia->is_open()) {
            duration<double, milli> tempo_total = high_resolution_clock::now() - inicio;

            *log_convergencia << fixed << setprecision(6)
                << T << "," << melhor_custo << "," << custo_s << "," << iter_total
                << "," << tempo_total.count() << "\n";
        }

        T *= alpha;

        // Verifica se houve melhora significativa neste ciclo (com tolerancia relativa de 1.0%)
        double tolerancia_melhora = 0.01; // 1.0% de tolerancia relativa

        if ((melhor_custo_ultima_verificacao - melhor_custo) > tolerancia_melhora * melhor_custo_ultima_verificacao) {
            ciclos_sem_melhoria = 0;                      // zera contador se houve melhora significativa
            melhor_custo_ultima_verificacao = melhor_custo;
        }
        else {
            ciclos_sem_melhoria++;                        // incrementa se nao houve melhora significativa
        }


        // Parada adaptativa se exceder o numero maximo de ciclos sem melhora
        if (usar_limiar_adaptativo && ciclos_sem_melhoria >= max_sem_melhora) {
            break;
        }

    }

    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;
    melhor.tempo_execucao_ms = solucao_inicial.tempo_execucao_ms + duracao.count();
    melhor.custo_total = melhor_custo;

    return melhor;
}


/**
 * @brief Funcao principal do programa VSBPP com metaheuristica Simulated Annealing.
 *
 * Esta versao (v05) executa o algoritmo SA com base no pseudo-codigo padrao,
 * realizando um estudo de sensibilidade com diferentes combinacoes de parametros T0, alpha e SAmax.
 * Para cada combinacao e para cada instancia, gera um arquivo CSV com a evolucao do custo e um resumo em .txt.
 *
 * Etapas:
 * - Loop por combinacoes de T0, alpha e SAmax
 * - Leitura da instancia do problema (arquivo .txt)
 * - Geracao de duas solucoes iniciais: Gulosa FFD e Aleatoria Viavel
 * - Aplicacao do Simulated Annealing com vizinhancas baseadas nos modos 1 a 5
 * - Registro do custo, tempo e reducao de custo no terminal e em arquivo .txt
 * - Registro da convergencia em arquivo .csv
 *
 * Arquivos gerados:
 * - 04.Simulated_Annealing_v05_Sensibilidade.txt: resumo dos resultados de cada combinacao
 * - 04.Convergencia_SA_<T0>_<alpha>_<SAmax>_<nome_instancia>.csv: log da evolucao da temperatura e custo
 *
 * @return 0 se a execucao terminar corretamente.
 */
int main()
{

    // Semente para controle de aleatoriedade
    const unsigned SEED_FIXA = 12345;
    pcg32 rng(SEED_FIXA); // gerador moderno com semente fixa


    // Lista de arquivos de instancia a serem processados (adicione ou remova conforme necessário)
    vector<string> nomes_instancias = {
                "H&Slin100-1-1.txt", "H&Slin100-1-2.txt", "H&Slin100-1-3.txt", "H&Slin100-1-4.txt", "H&Slin100-1-5.txt", "H&Slin100-1-6.txt", "H&Slin100-1-7.txt",
                "H&Slin100-1-8.txt", "H&Slin100-1-9.txt", "H&Slin100-1-10.txt", "H&Slin200-1-1.txt", "H&Slin200-1-2.txt", "H&Slin200-1-3.txt", "H&Slin200-1-4.txt",
                "H&Slin200-1-5.txt", "H&Slin200-1-6.txt", "H&Slin200-1-7.txt", "H&Slin200-1-8.txt", "H&Slin200-1-9.txt", "H&Slin200-1-10.txt", "H&Slin500-1-1.txt",
                "H&Slin500-1-2.txt", "H&Slin500-1-3.txt", "H&Slin500-1-4.txt", "H&Slin500-1-5.txt", "H&Slin500-1-6.txt", "H&Slin500-1-7.txt", "H&Slin500-1-8.txt",
                "H&Slin500-1-9.txt", "H&Slin500-1-10.txt", "H&Slin1000-1-1.txt", "H&Slin1000-1-2.txt", "H&Slin1000-1-3.txt", "H&Slin1000-1-4.txt", "H&Slin1000-1-5.txt",
                "H&Slin1000-1-6.txt", "H&Slin1000-1-7.txt", "H&Slin1000-1-8.txt", "H&Slin1000-1-9.txt", "H&Slin1000-1-10.txt", "H&Slin2000-1-1.txt", "H&Slin2000-1-2.txt",
                "H&Slin2000-1-3.txt", "H&Slin2000-1-4.txt", "H&Slin2000-1-5.txt", "H&Slin2000-1-6.txt", "H&Slin2000-1-7.txt", "H&Slin2000-1-8.txt", "H&Slin2000-1-9.txt",
                "H&Slin2000-1-10.txt", "H&Sconc100-2-1.txt", "H&Sconc100-2-2.txt", "H&Sconc100-2-3.txt", "H&Sconc100-2-4.txt", "H&Sconc100-2-5.txt", "H&Sconc100-2-6.txt",
                "H&Sconc100-2-7.txt", "H&Sconc100-2-8.txt", "H&Sconc100-2-9.txt", "H&Sconc100-2-10.txt", "H&Sconc200-2-1.txt", "H&Sconc200-2-2.txt", "H&Sconc200-2-3.txt",
                "H&Sconc200-2-4.txt", "H&Sconc200-2-5.txt", "H&Sconc200-2-6.txt", "H&Sconc200-2-7.txt", "H&Sconc200-2-8.txt", "H&Sconc200-2-9.txt", "H&Sconc200-2-10.txt",
                "H&Sconc500-2-1.txt", "H&Sconc500-2-2.txt", "H&Sconc500-2-3.txt", "H&Sconc500-2-4.txt", "H&Sconc500-2-5.txt", "H&Sconc500-2-6.txt", "H&Sconc500-2-7.txt",
                "H&Sconc500-2-8.txt", "H&Sconc500-2-9.txt", "H&Sconc500-2-10.txt", "H&Sconc1000-2-1.txt", "H&Sconc1000-2-2.txt", "H&Sconc1000-2-3.txt", "H&Sconc1000-2-4.txt",
                "H&Sconc1000-2-5.txt", "H&Sconc1000-2-6.txt", "H&Sconc1000-2-7.txt", "H&Sconc1000-2-8.txt", "H&Sconc1000-2-9.txt", "H&Sconc1000-2-10.txt", "H&Sconc2000-2-1.txt",
                "H&Sconc2000-2-2.txt", "H&Sconc2000-2-3.txt", "H&Sconc2000-2-4.txt", "H&Sconc2000-2-5.txt", "H&Sconc2000-2-6.txt", "H&Sconc2000-2-7.txt", "H&Sconc2000-2-8.txt",
                "H&Sconc2000-2-9.txt", "H&Sconc2000-2-10.txt", "H&Sconv100-3-1.txt", "H&Sconv100-3-2.txt", "H&Sconv100-3-3.txt", "H&Sconv100-3-4.txt", "H&Sconv100-3-5.txt",
                "H&Sconv100-3-6.txt", "H&Sconv100-3-7.txt", "H&Sconv100-3-8.txt", "H&Sconv100-3-9.txt", "H&Sconv100-3-10.txt", "H&Sconv200-3-1.txt", "H&Sconv200-3-2.txt",
                "H&Sconv200-3-3.txt", "H&Sconv200-3-4.txt", "H&Sconv200-3-5.txt", "H&Sconv200-3-6.txt", "H&Sconv200-3-7.txt", "H&Sconv200-3-8.txt", "H&Sconv200-3-9.txt",
                "H&Sconv200-3-10.txt", "H&Sconv500-3-1.txt", "H&Sconv500-3-2.txt", "H&Sconv500-3-3.txt", "H&Sconv500-3-4.txt", "H&Sconv500-3-5.txt", "H&Sconv500-3-6.txt",
                "H&Sconv500-3-7.txt", "H&Sconv500-3-8.txt", "H&Sconv500-3-9.txt", "H&Sconv500-3-10.txt", "H&Sconv1000-3-1.txt", "H&Sconv1000-3-2.txt", "H&Sconv1000-3-3.txt",
                "H&Sconv1000-3-4.txt", "H&Sconv1000-3-5.txt", "H&Sconv1000-3-6.txt", "H&Sconv1000-3-7.txt", "H&Sconv1000-3-8.txt", "H&Sconv1000-3-9.txt", "H&Sconv1000-3-10.txt",
                "H&Sconv2000-3-1.txt", "H&Sconv2000-3-2.txt", "H&Sconv2000-3-3.txt", "H&Sconv2000-3-4.txt", "H&Sconv2000-3-5.txt", "H&Sconv2000-3-6.txt", "H&Sconv2000-3-7.txt",
                "H&Sconv2000-3-8.txt", "H&Sconv2000-3-9.txt", "H&Sconv2000-3-10.txt" };


    // =======================================
    // Parametros para o estudo de sensibilidade
    // =======================================
    // Parâmetros do Simulated Annealing
    vector<double> lista_T0 = { 1000.0 };     // diferentes temperaturas iniciais
    vector<double> lista_alpha = { 0.95 };      // diferentes fatores de resfriamento
    vector<int> lista_SAmax = { 500 };            // diferentes quantidades de iteracoes por temperatura



    // Arquivo de saída para registrar resultados
    ofstream output("04.Simulated_Annealing_v05_Sensibilidade.txt");
    if (!output.is_open()) {
        cerr << "Erro ao criar o arquivo de saída!" << endl;
        return 1;
    }


    // Escolha do criterio de parada:
    // Se USAR_LIMIAR_ADAPTATIVO == true, ignora TEMPO_LIMITE_MS e para apos MAX_SEM_MELHORA ciclos sem melhora
    // Caso contrario, usa tempo limite como parada padrao
    const int TEMPO_LIMITE_MS = 150000;             // tempo limite em milissegundos
    const bool USAR_LIMIAR_ADAPTATIVO = true;      // se true, usa parada adaptativa
    const int MAX_SEM_MELHORA = 50;                 // numero maximo de ciclos sem melhora


    // ==============================================
    // Loop externo para todas as combinacoes de parametros
    // ==============================================
    for (double T0 : lista_T0) {
        for (double alpha : lista_alpha) {
            for (int SAmax : lista_SAmax) {

                // Loop interno para todas as instancias
                for (const auto& nomeInstancia_corrente : nomes_instancias) {

                    // Caminho completo do arquivo
                    string txtInstancia_corrente = "C:\\Users\\afons\\Pessoal\\03.Pos-Graduacao\\ITA\\Doutorado\\01.Disciplinas\\"
                        "PO-205_Metaheuristica\\Projeto\\Instances\\" + nomeInstancia_corrente;

                    // Estrutura da instancia
                    Instancia instancia_corrente;

                    // Leitura da instancia
                    if (!lerInstancia(txtInstancia_corrente, instancia_corrente)) {
                        cerr << "Erro ao ler a instancia: " << nomeInstancia_corrente << endl;
                        continue;
                    }

                    // ===============================
                    // CRIA ARQUIVO PARA LOG DE CONVERGENCIA
                    // ===============================
                    string nome_csv = "04.Convergencia_SA_" +
                        to_string(int(T0)) + "_" +
                        to_string(int(alpha * 100)) + "_" +
                        to_string(SAmax) + "_" +
                        nomeInstancia_corrente;

                    replace(nome_csv.begin(), nome_csv.end(), '.', '_'); // substitui pontos no nome
                    ofstream log_convergencia(nome_csv);

                    // Cria arquivo CSV para registrar a convergencia do SA (temperatura vs. melhor custo)
                    log_convergencia << "Temperatura,CustoMelhor,CustoAtual,IterTotal,TempoTotal_ms\n"; // cabecalho do CSV


                    // Cabeçalho no terminal e no arquivo
                    cout << "======= SA v05 | Instancia: " << nomeInstancia_corrente
                        << " | T0=" << T0 << " | alpha=" << alpha << " | SAmax=" << SAmax << " =======" << endl;

                    output << "======= SA v05 | Instancia: " << nomeInstancia_corrente
                        << " | T0=" << T0 << " | alpha=" << alpha << " | SAmax=" << SAmax << " =======\n";


                    //========= Gulosa FFD =========//

                    Solucao solucao_gulosa = construirSolucao(instancia_corrente, "gulosa_FFD", rng);
                    cout << "Custo Gulosa FFD: " << solucao_gulosa.custo_total
                        << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosa.tempo_execucao_ms << endl;
                    output << "Custo Gulosa FFD: " << solucao_gulosa.custo_total
                        << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosa.tempo_execucao_ms << "\n";

                    //====== Aleatoria Viavel ======//

                    Solucao solucao_aleatoria = construirSolucao(instancia_corrente, "aleatoria_viavel", rng);
                    cout << "Custo Aleatoria Viavel: " << solucao_aleatoria.custo_total
                        << " | Tempo (ms): " << fixed << setprecision(3) << solucao_aleatoria.tempo_execucao_ms << endl;
                    output << "Custo Aleatoria Viavel: " << solucao_aleatoria.custo_total
                        << " | Tempo (ms): " << fixed << setprecision(3) << solucao_aleatoria.tempo_execucao_ms << "\n";

                    //===== Simulated Annealing =====//

                    cout << "Iniciando Simulated Annealing com T0=" << T0
                        << ", alpha=" << alpha << ", SAmax=" << SAmax
                        << ", tempo limite=" << TEMPO_LIMITE_MS << " ms..." << endl;
                    output << "Iniciando Simulated Annealing com T0=" << T0
                        << ", alpha=" << alpha << ", SAmax=" << SAmax
                        << ", tempo limite=" << TEMPO_LIMITE_MS << " ms...\n";
                    if (USAR_LIMIAR_ADAPTATIVO) {
                        cout << "Parada adaptativa ativada: max " << MAX_SEM_MELHORA << " ciclos sem melhora.\n";
                        output << "Parada adaptativa ativada: max " << MAX_SEM_MELHORA << " ciclos sem melhora.\n";
                    }

                    // Variáveis adicionais para log
                    int vizinhos_aceitos = 0;             // numero de vizinhos aceitos (melhores ou piores)
                    int vizinhos_rejeitados = 0;          // numero de vizinhos rejeitados
                    int solucoes_piores_aceitas = 0;      // numero de solucoes piores aceitas por Metropolis

                    // Executa Simulated Annealing
                    Solucao solucao_otimizada = executarSimulatedAnnealing(
                        instancia_corrente, solucao_aleatoria, rng,
                        SAmax, T0, alpha, TEMPO_LIMITE_MS,
                        &log_convergencia,
                        vizinhos_aceitos,
                        vizinhos_rejeitados,
                        solucoes_piores_aceitas,
                        USAR_LIMIAR_ADAPTATIVO,
                        MAX_SEM_MELHORA);

                    // Resultado final
                    cout << "Custo Final (SA): " << solucao_otimizada.custo_total
                        << " | Tempo Total (ms): " << fixed << setprecision(3) << solucao_otimizada.tempo_execucao_ms << endl;
                    output << "Custo Final (SA): " << solucao_otimizada.custo_total
                        << " | Tempo Total (ms): " << fixed << setprecision(3) << solucao_otimizada.tempo_execucao_ms << "\n";

                    cout << "Reducao de custo: " << solucao_aleatoria.custo_total
                        << " -> " << solucao_otimizada.custo_total << endl << endl;
                    output << "Reducao de custo: " << solucao_aleatoria.custo_total
                        << " -> " << solucao_otimizada.custo_total << "\n\n";

                    // Calcula e imprime taxa média de ocupação
                    double taxa_ocupacao = calcularTaxaOcupacao(solucao_otimizada, instancia_corrente);

                    cout << "Taxa media de ocupacao: " << fixed << setprecision(4) << taxa_ocupacao << endl;
                    output << "Taxa media de ocupacao: " << fixed << setprecision(4) << taxa_ocupacao << "\n";

                    // Contadores adicionais (passados como referencia ao final do SA, se preferir)
                    output << "Total de vizinhos aceitos: " << vizinhos_aceitos << "\n";
                    output << "Total de vizinhos rejeitados: " << vizinhos_rejeitados << "\n";
                    output << "Solucoes piores aceitas (Metropolis): " << solucoes_piores_aceitas << "\n";

                    if (USAR_LIMIAR_ADAPTATIVO)
                        output << "Parada por adaptativo (sem melhora): " << MAX_SEM_MELHORA << " ciclos.\n";
                    else
                        output << "Parada por tempo limite: " << TEMPO_LIMITE_MS << " ms.\n";


                    log_convergencia.close(); // Fecha o arquivo CSV de log de convergencia ao final da instancia

                } // fim do loop de instancias
            } // fim do loop SAmax
        } // fim do loop alpha
    } // fim do loop T0

    output.close();
    return 0;

} // fim do main
