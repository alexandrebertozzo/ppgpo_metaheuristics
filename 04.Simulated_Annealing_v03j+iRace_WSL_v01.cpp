/**
 * @file 04.Simulated_Annealing_v03j+iRace_WSL_v01.cpp
 * @brief Simulated Annealing para o problema VSBPP com suporte a FFD, BFD e modularizacao da geracao de vizinhos.
 *
 * Esta versao aplica o algoritmo Simulated Annealing ao problema do Bin Packing com Varios Tipos de Bin (VSBPP),
 * utilizando uma arquitetura modularizada permitindo comparar diferentes heuristicas
 * de solucao inicial, incluindo:
 *
 * - Tipo 0: "aleatoria_viavel": construcao aleatoria respeitando capacidades;
 * - Tipo 1: "melhor_de_N_aleatorias": melhor entre N solucoes aleatorias viaveis;
 * - Tipo 2: "gulosa_FFD": First Fit Decreasing (tipo mais barato viavel);
 * - Tipo 3: "gulosa_FFD_extremos": tipo sorteado entre o mais barato e o mais caro viavel;
 * - Tipo 4: "gulosa_FFD_topKbaratos": tipo aleatorio entre os 3 mais baratos viaveis;
 * - Tipo 5: "gulosa_FFD_topKcaros": tipo aleatorio entre os 3 mais caros viaveis;
  * - Tipo 6: melhor entre as seis anteriores (comparacao direta por custo_total).
 *
 * e a geracao de vizinhos:
 *
 * - `gerarUnicoVizinho(...)`: gera um vizinho viavel com base em um modo especifico (1 a 3);
 * - `gerarMultiplosVizinhos(...)`: gera varios vizinhos repetindo `gerarUnicoVizinho`;
 * - `executarSimulatedAnnealing(...)`: aplica SA com Metropolis, T0, alpha e SAmax;
 * - `executarDescent(...)`: realiza busca local com tempo limite.
 *
 * Os modos de vizinhanca implementados sao:
 * - Modo 1: troca o tipo de um bin por outro viavel;
 * - Modo 2: troca itens entre dois bins;
 * - Modo 3: remove um bin se for possivel realocar seus itens;
 * - Modo 4: move um item de um bin para outro com capacidade;
 * - Modo 5: troca dois pares de itens entre dois bins, mantendo viabilidade.
 *
 * Entradas:
 * - Arquivos `.txt` contendo instancias no formato H&S (capacidades, custos, pesos).
 *
 * Saidas:
 * - Resultados resumidos em `.txt`;
 * - Convergencia do SA em `.csv`.
 *
 * Esta arquitetura permite evolucao para meta-heuristicas como GRASP, ILS, VNS, GA etc.
 *
 * @author Afonso, Chat GPT
 * @version v03j+
 * @date Maio 2025
 *
 * @details Esta versao implementa:
 *          (i)   avaliacao incremental de custo para o modo 1;
 *          (ii)  cache da funcao objetivo com unordered_map<uint64_t, int>;
 *          (iii) controle de repeticao de solucoes via unordered_set<uint64_t>;
 *          (iv)  parametro global para debug e controle de logs;
 *          (v)   selecao entre 7 tipos de construcao inicial (aleatoria, melhor de N, FFD e 3 variacoes, melhor global);
 *          (vi)  mecanismo de comparacao direta entre todas as solucoes iniciais para TIPO_SOLUCAO_INICIAL == 6.
 */


 // =======================
 // DEFINICOES DO PCG: DEVEM VIR ANTES DE QUALQUER HEADER
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
#include <random>        // pode permanecer, se usado com uniform distributions
#include <unordered_set> // para eliminar vizinhos duplicados
#include <functional>    // para usar std::hash
#include <future>        // para usar std::async e std::future na paralelizacao
#include <thread>        // para sleep_for
#include <unordered_map> // necessário para cache da funcao objetivo


using namespace std;

// =======================
// CACHE GLOBAL DE CUSTOS DE SOLUÇÕES
// =======================
unordered_map<uint64_t, int> cacheCustos;  // cache da funcao objetivo

// Este conjunto guarda o hash de cada solucao ja aceita durante a execucao do SA.
// Impede que o algoritmo aceite vizinhos repetidos, mesmo com diferentes operacoes.
unordered_set<uint64_t> historicoHashes;   // conjunto global para evitar repeticoes de solucoes


// Parametro para definir a politica de escolha de tipo de bin
// 0 = sempre o tipo mais barato viavel (FFD classico)
// 1 = aleatorio entre os k mais baratos e os k mais caros
// 2 = aleatorio entre os k mais caros viaveis
// 3 = aleatorio entre os k mais baratos viaveis
int MODO_ESCOLHA_TIPO_BIN = 0;


// Declaracao global de parametros controlados pelo iRACE
int NUM_SOLUCOES_ALEATORIAS;
int NUM_TOP_K_TIPOS_BIN;
int K_TROCA_ITENS_BIN;
int CRITERIO_MELHOR_ENTRE_N;
double LIMIAR_OCUPACAO_BIN;
int MAX_TENTATIVAS;

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
 * @brief Gera um hash deterministico da solucao com base no tipo e ordem dos itens de cada bin.
 *
 * O hash considera:
 * - A ordem dos bins na solucao;
 * - O tipo de cada bin;
 * - A ordem e identidade dos itens alocados em cada bin.
 *
 * Usado para evitar solucoes repetidas no SA (via historicoHashes)
 * e para consultar custos ja calculados (via cacheCustos).
 *
 * @param sol Solucao a ser hasheada.
 * @return Valor de hash FNV-1a (uint64_t).
 */
uint64_t calcularHashSolucao(const Solucao& sol) {
    uint64_t hash = 0xcbf29ce484222325ULL; // valor inicial do FNV-1a
    const uint64_t prime = 0x100000001b3ULL;

    for (const BinUsado& bin : sol.bins) {
        hash ^= static_cast<uint64_t>(bin.tipo);
        hash *= prime;

        for (int item : bin.itens) {
            hash ^= static_cast<uint64_t>(item);
            hash *= prime;
        }
    }

    return hash;
}

/**
 * @brief Calcula o custo total da solucao usando cache.
 *
 * Verifica se o hash da solucao esta presente no cache.
 * Se estiver, retorna o custo armazenado.
 * Caso contrario, calcula o custo, armazena no cache e retorna.
 *
 * @param solucao Solucao atual.
 * @param instancia Dados da instancia.
 * @return Custo total da solucao.
 */
int calcularCustoTotal(const Solucao& solucao, const Instancia& instancia) {
    uint64_t hash = calcularHashSolucao(solucao);

    auto it = cacheCustos.find(hash);
    if (it != cacheCustos.end()) {
        return it->second; // custo ja calculado
    }

    int custo_total = 0;
    for (const BinUsado& bin : solucao.bins) {
        int tipo = bin.tipo;
        custo_total += instancia.tipos[tipo].custo;
    }

    cacheCustos[hash] = custo_total; // armazena no cache
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

        // Apenas adiciona o novo bin na solucao se o mesmo nao estiver ocupado dentro do limiar
        if (!novo_bin.itens.empty()) {
            int capacidade_usada = 0;
            for (int idx : novo_bin.itens)
                capacidade_usada += instancia_corrente.itens[idx].peso;

            double ocupacao = static_cast<double>(capacidade_usada) / instancia_corrente.tipos[novo_bin.tipo].capacidade;

            if (ocupacao >= LIMIAR_OCUPACAO_BIN) {
                solucao_corrente.bins.push_back(novo_bin);
            }
        }

    }

    // Custo total da solucao com cache
    solucao_corrente.custo_total = calcularCustoTotal(solucao_corrente, instancia_corrente);


    // Tempo total da solução
    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;
    solucao_corrente.tempo_execucao_ms = duracao.count();

    // Retorna a solucao viavel gerada
    return solucao_corrente;
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
 * @brief Gera N solucoes aleatorias viaveis e retorna a de menor custo.
 *
 * Esta versao executa NUM_SOLUCOES_ALEATORIAS construtores aleatorios com seeds distintas
 * e retorna a melhor solucao (menor custo total) entre elas.
 *
 * @param instancia_corrente Estrutura com os dados da instancia atual.
 * @param rng Gerador aleatorio base (usado para gerar seeds locais).
 * @param NUM_SOLUCOES_ALEATORIAS Numero de solucoes aleatorias a gerar.
 * @return Solucao de menor custo entre as N geradas.
 */
Solucao gerarMelhorSolucaoAleatoriaViavel(const Instancia& instancia_corrente, pcg32& rng, int NUM_SOLUCOES_ALEATORIAS)
{
    vector<Solucao> candidatas;
    candidatas.reserve(NUM_SOLUCOES_ALEATORIAS);

    for (int i = 0; i < NUM_SOLUCOES_ALEATORIAS; ++i) {
        pcg32 rng_local(rng()); // gera nova seed com base em rng
        candidatas.push_back(gerarSolucaoAleatoriaViavel(instancia_corrente, rng_local));
    }

    // 0 = menor custo, 1 = maior ocupação, 2 = função híbrida
    return *min_element(candidatas.begin(), candidatas.end(),
        [&](const Solucao& a, const Solucao& b) {
            if (CRITERIO_MELHOR_ENTRE_N == 0)
                return a.custo_total < b.custo_total;

            else if (CRITERIO_MELHOR_ENTRE_N == 1)
                return calcularTaxaOcupacao(a, instancia_corrente) > calcularTaxaOcupacao(b, instancia_corrente);

            else if (CRITERIO_MELHOR_ENTRE_N == 2) {
                double score_a = calcularTaxaOcupacao(a, instancia_corrente) / a.custo_total;
                double score_b = calcularTaxaOcupacao(b, instancia_corrente) / b.custo_total;
                return score_a > score_b;
            }

            cerr << "Erro: CRITERIO_MELHOR_ENTRE_N invalido!\n";
            exit(EXIT_FAILURE);
        });
}


/**
 * @brief Seleciona um tipo de bin viavel baseado no MODO_ESCOLHA_TIPO_BIN.
 *
 * @param instancia Dados da instancia com tipos de bin.
 * @param peso Peso do item a ser alocado.
 * @param rng Gerador aleatorio.
 * @return Indice do tipo de bin escolhido.
 */
int escolherTipoBinViavel(const Instancia& instancia, int peso, pcg32& rng)
{
    // Lista dos indices dos tipos de bin que conseguem acomodar o item
    vector<int> viaveis;
    for (int i = 0; i < instancia.m; ++i)
        if (instancia.tipos[i].capacidade >= peso)
            viaveis.push_back(i);

    // Se nenhum tipo suporta o item, gera erro fatal
    if (viaveis.empty()) {
        cerr << "Erro: nenhum tipo de bin comporta item com peso " << peso << endl;
        exit(EXIT_FAILURE);
    }

    // MODO 0 = FFD classico: retorna o tipo de bin viavel com menor custo
    if (MODO_ESCOLHA_TIPO_BIN == 0) {
        return *min_element(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo < instancia.tipos[b].custo;
            });
    }

    // MODO 1 = sorteia entre os NUM_TOP_K_TIPOS_BIN mais baratos e mais caros
    if (MODO_ESCOLHA_TIPO_BIN == 1) {
        // Ordena por custo crescente
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo < instancia.tipos[b].custo;
            });

        // Determina limite inferior e superior (K primeiros e K ultimos)
        int limite = min(NUM_TOP_K_TIPOS_BIN, static_cast<int>(viaveis.size()));
        vector<int> opcoes_kbaratos(viaveis.begin(), viaveis.begin() + limite);
        vector<int> opcoes_kcaros(viaveis.end() - limite, viaveis.end());

        // Une os dois vetores em uma só lista de opcoes
        vector<int> opcoes;
        opcoes.reserve(opcoes_kbaratos.size() + opcoes_kcaros.size());
        opcoes.insert(opcoes.end(), opcoes_kbaratos.begin(), opcoes_kbaratos.end());
        opcoes.insert(opcoes.end(), opcoes_kcaros.begin(), opcoes_kcaros.end());

        // Sorteia aleatoriamente entre as opcoes combinadas
        uniform_int_distribution<int> d(0, static_cast<int>(opcoes.size()) - 1);
        return opcoes[d(rng)];
    }


    // MODO 2 = sorteia entre os NUM_TOP_K_TIPOS_BIN mais caros entre os viaveis
    if (MODO_ESCOLHA_TIPO_BIN == 2) {
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo > instancia.tipos[b].custo;
            });
        int limite = min(NUM_TOP_K_TIPOS_BIN, static_cast<int>(viaveis.size()));
        uniform_int_distribution<int> d(0, limite - 1);
        return viaveis[d(rng)];
    }


    // MODO 3 = sorteia entre os NUM_TOP_K_TIPOS_BIN mais baratos entre os viaveis
    if (MODO_ESCOLHA_TIPO_BIN == 3) {
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo < instancia.tipos[b].custo;
            });
        int limite = min(NUM_TOP_K_TIPOS_BIN, static_cast<int>(viaveis.size()));
        uniform_int_distribution<int> d(0, limite - 1);
        return viaveis[d(rng)];
    }


    // Qualquer modo invalido aciona erro
    cerr << "Erro: MODO_ESCOLHA_TIPO_BIN invalido!" << endl;
    exit(EXIT_FAILURE);
}


/**
 * @brief Gera uma solucao baseada em FFD com politica flexivel de selecao de tipo de bin.
 *
 * @param instancia_corrente Instancia do problema.
 * @param rng Gerador aleatorio.
 * @param modo Tipo de politica (0: FFD classico, 1: extremos, 2: topKcaros, 3: topKbaratos).
 * @return Solucao gerada conforme a politica de tipo.
 */
Solucao construirSolucaoFFDComPolitica(const Instancia& instancia_corrente, pcg32& rng, int modo)
{
    using namespace std::chrono;
    auto inicio = high_resolution_clock::now();

    Solucao solucao_corrente;

    // Define a politica global a ser usada na escolha do tipo de bin
    MODO_ESCOLHA_TIPO_BIN = modo;

    // Ordena os itens em ordem decrescente de peso (FFD)
    vector<int> indices_itens(instancia_corrente.n);
    iota(indices_itens.begin(), indices_itens.end(), 0);
    sort(indices_itens.begin(), indices_itens.end(), [&](int a, int b) {
        return instancia_corrente.itens[a].peso > instancia_corrente.itens[b].peso;
        });

    vector<int> capacidades_restantes; // Capacidade restante de cada bin aberto

    // Tenta alocar cada item na ordem definida
    for (int idx : indices_itens) {
        int peso = instancia_corrente.itens[idx].peso;
        bool alocado = false;

        // Tenta alocar o item em um bin ja aberto com capacidade suficiente
        for (size_t i = 0; i < solucao_corrente.bins.size(); ++i) {
            if (capacidades_restantes[i] >= peso) {
                solucao_corrente.bins[i].itens.push_back(idx);
                capacidades_restantes[i] -= peso;
                alocado = true;
                break;
            }
        }

        // Se nao alocou, abre um novo bin com tipo definido pela politica ativa
        if (!alocado) {
            int tipo = escolherTipoBinViavel(instancia_corrente, peso, rng);
            BinUsado novo;
            novo.tipo = tipo;
            novo.itens.push_back(idx);
            solucao_corrente.bins.push_back(novo);
            capacidades_restantes.push_back(instancia_corrente.tipos[tipo].capacidade - peso);
        }
    }

    // Calcula o custo total da solucao e o tempo de execucao
    solucao_corrente.custo_total = calcularCustoTotal(solucao_corrente, instancia_corrente);
    solucao_corrente.tempo_execucao_ms = duration<double, milli>(high_resolution_clock::now() - inicio).count();
    return solucao_corrente;
}


/**
 * @brief: Funcao central de construcao da solucao, que escolhe a estrategia de acordo com o parametro.
 *
 * Esta funcao permite selecionar entre diferentes heuristicas de construcao inicial:
 * - "aleatoria_viavel": aloca itens aleatoriamente respeitando a capacidade dos bins;
 * - "melhor_de_N_aleatorias": escolhe a Melhor dentre N aleatorias viaveis
 * - "gulosa_FFD": aplica FFD com tipo de bin mais barato (classico);
 * - "gulosa_FFD_extremos": aplica FFD com tipo aleatorio entre o mais barato e o mais caro viavel;
 * - "gulosa_FFD_topKcaros": aplica FFD com tipo aleatorio entre os 3 mais caros viaveis;
 * - "gulosa_FFD_topKbaratos": aplica FFD com tipo aleatorio entre os 3 mais baratos viaveis;
 *
 * A escolha e feita com base na string informada no parametro 'metodo'.
 *
 *    @param instancia_corrente Estrutura com os dados da instancia atual.
 *    @param metodo Nome do metodo de construcao ("aleatoria_viavel", "gulosa_FFD", "gulosa_BFD").
 *    @param rng Gerador aleatorio (necessario para "aleatoria_viavel").
 *    @return Estrutura Solucao preenchida conforme a estrategia escolhida.
 */
Solucao construirSolucao(const Instancia& instancia_corrente, const string& metodo, pcg32& rng)
{
    // Caso a estrategia escolhida seja "aleatoria" que respeita capacidade dos bins
    if (metodo == "aleatoria_viavel") {
        return gerarSolucaoAleatoriaViavel(instancia_corrente, rng);
    }

    // Caso a estrategia escolhida seja Melhor dentre N aleatorias viaveis
    else if (metodo == "melhor_de_N_aleatorias") {

        return gerarMelhorSolucaoAleatoriaViavel(instancia_corrente, rng, NUM_SOLUCOES_ALEATORIAS);
    }

    // Caso a estrategia escolhida seja uma das vaariações "gulosas" do FFD
    else if (metodo == "gulosa_FFD") {
        return construirSolucaoFFDComPolitica(instancia_corrente, rng, 0);
    }
    else if (metodo == "gulosa_FFD_extremos") {
        return construirSolucaoFFDComPolitica(instancia_corrente, rng, 1);
    }
    else if (metodo == "gulosa_FFD_topKcaros") {
        return construirSolucaoFFDComPolitica(instancia_corrente, rng, 2);
    }
    else if (metodo == "gulosa_FFD_topKbaratos") {
        return construirSolucaoFFDComPolitica(instancia_corrente, rng, 3);
    }

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
 * @brief Gera apenas um vizinho aleatorio viavel a partir do modo informado.
 *
 * Esta funcao utiliza o modo de vizinhanca informado (1 a 5) e retorna o primeiro vizinho
 * valido gerado. Ideal para meta-heuristicas baseadas em amostragem estocastica como Simulated Annealing.
 *
 * A partir da versao v03j+, o modo 1 realiza atualizacao incremental do custo:
 * - Ao trocar o tipo de um bin, o custo_total e decrementado pelo custo antigo
 *   e incrementado pelo custo do novo tipo, mantendo a consistencia da solucao.
 *
 * Modos implementados:
 * - 1: Move um item aleatorio para outro bin (com custo incremental).
 * - 2: Troca dois itens entre dois bins.
 * - 3: Move item para um novo bin.
 * - 4: Move item de um bin para outro com capacidade.
 * - 5: Troca dois pares de itens entre dois bins.
 *
 * @param solucao Solucao atual.
 * @param instancia Dados da instancia.
 * @param rng Gerador aleatorio (pcg32).
 * @param modo Modo de vizinhanca (1 a 5).
 * @return Vizinho gerado ou copia da solucao original se nenhum vizinho for viavel.
 */


Solucao gerarUnicoVizinho(const Solucao& solucao, const Instancia& instancia, pcg32& rng, int modo)
{
    if (modo == 1) {
        // Embaralha a ordem dos bins para evitar determinismo
        vector<size_t> ordem(solucao.bins.size());
        iota(ordem.begin(), ordem.end(), 0);
        shuffle(ordem.begin(), ordem.end(), rng);

        for (size_t idx : ordem) {
            int carga = calcularCarga(solucao.bins[idx], instancia);
            for (int t = 0; t < instancia.m; ++t) {
                if (t != solucao.bins[idx].tipo && instancia.tipos[t].capacidade >= carga) {

                    Solucao v = solucao;
                    int tipo_antigo = v.bins[idx].tipo;                   // salva o tipo antigo antes da mudanca
                    v.custo_total -= instancia.tipos[tipo_antigo].custo;  // subtrai custo antigo
                    v.bins[idx].tipo = t;                                 // aplica nova mudanca
                    v.custo_total += instancia.tipos[t].custo;            // adiciona custo do novo tipo

                    return v;
                }
            }
        }
    }

    else if (modo == 2) {
        // Embaralha a ordem dos pares de bins para maior variabilidade
        vector<size_t> ordem_a(solucao.bins.size());
        iota(ordem_a.begin(), ordem_a.end(), 0);
        shuffle(ordem_a.begin(), ordem_a.end(), rng);

        for (size_t a : ordem_a) {
            vector<size_t> ordem_b(solucao.bins.size());
            iota(ordem_b.begin(), ordem_b.end(), 0);
            shuffle(ordem_b.begin(), ordem_b.end(), rng);

            for (size_t b : ordem_b) {
                if (a >= b) continue;

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

                            // custo total nao muda pois os tipos de bin nao mudaram
                            v.custo_total = solucao.custo_total;

                            return v;
                        }
                    }
                }
            }
        }
    }

    else if (modo == 3) {
        // Embaralha a ordem dos bins a serem removidos
        vector<size_t> ordem(solucao.bins.size());
        iota(ordem.begin(), ordem.end(), 0);
        shuffle(ordem.begin(), ordem.end(), rng);

        for (size_t i : ordem) {
            const BinUsado& bin_remover = solucao.bins[i];
            vector<int> itens_a_realocar = bin_remover.itens;

            Solucao v = solucao;
            v.bins.erase(v.bins.begin() + i);
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
                v.custo_total = calcularCustoTotal(v, instancia);  // atualiza custo total apos alteracao estrutural
                return v;
            }
        }
    }


    else if (modo == 4) {
        // MODO 4: mover um item de um bin para outro com capacidade
        vector<size_t> ordem_origem(solucao.bins.size());
        iota(ordem_origem.begin(), ordem_origem.end(), 0);
        shuffle(ordem_origem.begin(), ordem_origem.end(), rng);

        for (size_t a : ordem_origem) {
            if (solucao.bins[a].itens.empty()) continue;

            vector<size_t> ordem_destino(solucao.bins.size());
            iota(ordem_destino.begin(), ordem_destino.end(), 0);
            shuffle(ordem_destino.begin(), ordem_destino.end(), rng);

            for (size_t b : ordem_destino) {
                if (a == b) continue;

                const BinUsado& binA = solucao.bins[a];
                const BinUsado& binB = solucao.bins[b];

                int capB = instancia.tipos[binB.tipo].capacidade;
                int cargaB = calcularCarga(binB, instancia);

                for (int item_idx : binA.itens) {
                    int peso = instancia.itens[item_idx].peso;
                    if (cargaB + peso <= capB) {
                        Solucao v = solucao;
                        auto& va = v.bins[a].itens;
                        auto& vb = v.bins[b].itens;

                        va.erase(remove(va.begin(), va.end(), item_idx), va.end());
                        vb.push_back(item_idx);

                        // custo permanece o mesmo
                        v.custo_total = solucao.custo_total;

                        return v;
                    }
                }
            }
        }
    }

    else if (modo == 5) {
        // MODO 5: troca K itens entre dois bins
        int tentativas = 0;

        vector<size_t> ordem_a(solucao.bins.size());
        iota(ordem_a.begin(), ordem_a.end(), 0);
        shuffle(ordem_a.begin(), ordem_a.end(), rng);

        for (size_t a : ordem_a) {
            vector<size_t> ordem_b(solucao.bins.size());
            iota(ordem_b.begin(), ordem_b.end(), 0);
            shuffle(ordem_b.begin(), ordem_b.end(), rng);

            for (size_t b : ordem_b) {
                if (a >= b) continue;

                if (++tentativas > MAX_TENTATIVAS)
                    return solucao;  // fallback por excesso de tentativas


                const BinUsado& binA = solucao.bins[a];
                const BinUsado& binB = solucao.bins[b];

                if (binA.itens.size() < static_cast<size_t>(K_TROCA_ITENS_BIN) ||
                    binB.itens.size() < static_cast<size_t>(K_TROCA_ITENS_BIN))
                    continue;

                // Amostragem aleatoria de K itens de cada bin
                vector<int> itensA = binA.itens;
                vector<int> itensB = binB.itens;

                shuffle(itensA.begin(), itensA.end(), rng);
                shuffle(itensB.begin(), itensB.end(), rng);

                vector<int> selecaoA(itensA.begin(), itensA.begin() + K_TROCA_ITENS_BIN);
                vector<int> selecaoB(itensB.begin(), itensB.begin() + K_TROCA_ITENS_BIN);

                // Verifica capacidade
                int pesoA = 0, pesoB = 0;
                for (int i : selecaoA) pesoA += instancia.itens[i].peso;
                for (int j : selecaoB) pesoB += instancia.itens[j].peso;

                int cargaA = calcularCarga(binA, instancia);
                int cargaB = calcularCarga(binB, instancia);

                int capA = instancia.tipos[binA.tipo].capacidade;
                int capB = instancia.tipos[binB.tipo].capacidade;

                if ((cargaA - pesoA + pesoB <= capA) && (cargaB - pesoB + pesoA <= capB)) {
                    Solucao v = solucao;
                    auto& va = v.bins[a].itens;
                    auto& vb = v.bins[b].itens;

                    // Remove selecaoA de A e adiciona selecaoB
                    for (int i : selecaoA)
                        va.erase(remove(va.begin(), va.end(), i), va.end());
                    va.insert(va.end(), selecaoB.begin(), selecaoB.end());

                    // Remove selecaoB de B e adiciona selecaoA
                    for (int j : selecaoB)
                        vb.erase(remove(vb.begin(), vb.end(), j), vb.end());
                    vb.insert(vb.end(), selecaoA.begin(), selecaoA.end());

                    v.custo_total = solucao.custo_total;  // os tipos nao mudaram
                    return v;
                }
            }
        }
    }

    return solucao; // fallback se nenhum vizinho foi possivel
}

/**
 * @brief Gera multiplos vizinhos viaveis com base em um modo de vizinhanca fixo.
 *
 * Esta funcao chama iterativamente gerarUnicoVizinho para construir um vetor
 * com ate max_vizinhos solucoes diferentes da original. A repeticao termina
 * se nenhum novo vizinho viavel puder ser gerado.
 *
 * Ideal para buscas locais deterministicas como Descent ou para populacoes em GRASP, VNS etc.
 *
 * @param solucao Solucao base.
 * @param instancia Dados da instancia.
 * @param rng Gerador aleatorio.
 * @param max_vizinhos Numero maximo de vizinhos desejados.
 * @param modo Inteiro representando o modo de vizinhanca (1, 2 ou 3).
 * @return Vetor de vizinhos distintos e viaveis.
 */
vector<Solucao> gerarMultiplosVizinhos(const Solucao& solucao, const Instancia& instancia, pcg32& rng, int max_vizinhos, int modo)
{
    vector<Solucao> vizinhos;
    while (static_cast<int>(vizinhos.size()) < max_vizinhos) {
        Solucao v = gerarUnicoVizinho(solucao, instancia, rng, modo);
        if (v.bins != solucao.bins) {
            vizinhos.push_back(v);
        }
        else {
            break; // nenhum novo vizinho viavel
        }
    }

    return vizinhos;
}


/**
 * @brief Executa a busca local Descent com os modos de vizinhanca 1 a 3.
 *
 * A cada iteracao, testa-se a aplicacao dos modos 1 a 3 (individualmente),
 * cada um gerando ate max_vizinhos / 3 vizinhos. O melhor vizinho com custo
 * inferior ao atual e adotado como nova solucao. O processo se repete
 * enquanto houver melhora e nao for excedido o tempo limite.
 *
 * @param instancia Dados da instancia.
 * @param solucao_inicial Solucao inicial da busca.
 * @param rng Gerador aleatorio.
 * @param max_vizinhos Numero maximo total de vizinhos por iteracao (dividido entre os modos).
 * @param tempo_limite_ms Tempo limite em milissegundos para a execucao da busca local.
 * @return Melhor solucao encontrada dentro do tempo limite.
 *
 * A função executarDescent(...) ainda recalcula calcularCustoTotal(...) para todos os vizinhos,
 * o que é aceitável por hora, pois ela não está sendo usada em nenhum trecho do main() nesta versão.
 *
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
        for (int modo = 1; modo <= 3; ++modo) {
            vector<Solucao> vizinhos_parciais = gerarMultiplosVizinhos(melhor, instancia, rng, max_vizinhos / 3, modo);
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
 * @brief Executa o algoritmo Simulated Annealing (SA) para o VSBPP com cinco modos de vizinhanca.
 *
 * Esta funcao utiliza cache da funcao objetivo e prevencao de repeticao de solucoes via hash.
 * A cada temperatura T, realiza SAmax iteracoes. Em cada iteracao:
 * - Sorteia um modo de vizinhanca (1 a 5);
 * - Gera um vizinho com base nesse modo;
 * - Verifica se o hash da solucao ja foi visitado;
 * - Aceita solucoes melhores ou piores segundo criterio de Metropolis.
 *
 * A temperatura e reduzida multiplicativamente (T <- alpha * T) e o processo
 * termina ao atingir o tempo limite. Para cada temperatura, registra-se no CSV:
 * - Quantas solucoes piores foram aceitas (criterio de Metropolis);
 * - Quantas solucoes foram aceitas por cada modo de vizinhanca (1 a 5);
 * - Total de iteracoes acumuladas.
 *
 * Modos de vizinhanca:
 * - Modo 1: troca o tipo de um bin por outro viavel;
 * - Modo 2: troca de itens entre dois bins;
 * - Modo 3: remove um bin e tenta realocar seus itens;
 * - Modo 4: move um item de um bin para outro com capacidade;
 * - Modo 5: troca dois pares de itens entre dois bins.
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
    int& solucoes_piores_aceitas)
{
    using namespace chrono;
    auto inicio = high_resolution_clock::now();

    Solucao s = solucao_inicial;
    int custo_s = s.custo_total;  // ja calculado anteriormente
    historicoHashes.insert(calcularHashSolucao(s));  // registra solucao inicial

    Solucao melhor = s;
    int melhor_custo = custo_s;


    double T = T0;
    int IterT = 0;

    uniform_real_distribution<double> prob_dist(0.0, 1.0);

    int iter_total = 0;                    // total de iteracoes acumuladas

    while (true) {
        auto agora = high_resolution_clock::now();
        duration<double, milli> tempo_decorrido = agora - inicio;
        if (tempo_decorrido.count() > tempo_limite_ms)
            break;

        IterT = 0;

        int aceitacoes_piores_na_temp = 0;
        int modo1_aceitos = 0;
        int modo2_aceitos = 0;
        int modo3_aceitos = 0;
        int modo4_aceitos = 0;
        int modo5_aceitos = 0;


        while (IterT < SAmax) {
            IterT++;

            // Escolhe modo de vizinhanca aleatoriamente (1 a 5)
            uniform_int_distribution<int> modo_dist(1, 5);
            int modo = modo_dist(rng);

            // Gera um vizinho aleatorio (apenas 1)
            Solucao s_linha = gerarUnicoVizinho(s, instancia, rng, modo);
            if (s_linha.bins == s.bins) continue; // sem vizinho viavel


            // Calcula hash do vizinho gerado
            uint64_t hash_s_linha = calcularHashSolucao(s_linha);

            // Se a solucao ja foi visitada, ignora
            if (historicoHashes.count(hash_s_linha)) {
                vizinhos_rejeitados++;
                continue;
            }

            // Calcula delta normalmente
            int custo_s_linha = s_linha.custo_total;  // ja atualizado em gerarUnicoVizinho
            int delta = custo_s_linha - custo_s;


            iter_total++; // incremento acumulado de iteracoes

            if (delta <= 0) {
                s = s_linha;
                custo_s = custo_s_linha;
                historicoHashes.insert(hash_s_linha);  // registra o hash da nova solucao aceita

                vizinhos_aceitos++;

                // incrementa contador por modo
                if (modo == 1) modo1_aceitos++;
                else if (modo == 2) modo2_aceitos++;
                else if (modo == 3) modo3_aceitos++;
                else if (modo == 4) modo4_aceitos++;
                else if (modo == 5) modo5_aceitos++;

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
                    historicoHashes.insert(hash_s_linha);  // registra o hash da nova solucao aceita

                    solucoes_piores_aceitas++;
                    aceitacoes_piores_na_temp++;
                    vizinhos_aceitos++;

                    if (modo == 1) modo1_aceitos++;
                    else if (modo == 2) modo2_aceitos++;
                    else if (modo == 3) modo3_aceitos++;
                    else if (modo == 4) modo4_aceitos++;
                    else if (modo == 5) modo5_aceitos++;

                }
                else {
                    vizinhos_rejeitados++;
                }
            }



            auto tempo_atual = high_resolution_clock::now();
            duration<double, milli> tempo_total = tempo_atual - inicio;
            if (tempo_total.count() > tempo_limite_ms)
                break;
        }

        // Registro da convergencia com colunas adicionais
        if (log_convergencia && log_convergencia->is_open()) {
            duration<double, milli> tempo_total = high_resolution_clock::now() - inicio;

            *log_convergencia << fixed << setprecision(6)
                << tempo_total.count() << "," << T << "," << melhor_custo << "," << custo_s << "," << iter_total << ","
                << aceitacoes_piores_na_temp << "," << modo1_aceitos << "," << modo2_aceitos << "," << modo3_aceitos << ","
                << modo4_aceitos << "," << modo5_aceitos << "\n";

            log_convergencia->flush(); // <- esta linha garante salvamento incremental real	
        }

        // Imprime no terminal os contadores da temperatura atual
        //cout << "AceitacoesPiores: " << aceitacoes_piores_na_temp
        //    << " | Modo1: " << modo1_aceitos
        //    << " | Modo2: " << modo2_aceitos
        //    << " | Modo3: " << modo3_aceitos
        //    << " | Modo4: " << modo4_aceitos
        //    << " | Modo5: " << modo5_aceitos << endl;

        T *= alpha;
    }

    auto fim = high_resolution_clock::now();
    duration<double, milli> duracao = fim - inicio;
    melhor.tempo_execucao_ms = solucao_inicial.tempo_execucao_ms + duracao.count();
    // melhor.custo_total ja foi mantido atualizado)


    return melhor;
}

/**
 * @brief Repete o conteudo de um vetor base por N vezes consecutivas.
 *
 * @tparam T Tipo dos elementos do vetor.
 * @param base Vetor com os elementos a serem repetidos.
 * @param repeticoes Numero de vezes que o vetor base deve ser concatenado.
 * @return Vetor com o conteudo repetido.
 */
template<typename T>
vector<T> gerarListaCombinada(const vector<T>& base, int repeticoes)
{
    vector<T> resultado;
    resultado.reserve(base.size() * repeticoes); // opcional: otimiza alocacao

    for (int r = 0; r < repeticoes; ++r) {
        resultado.insert(resultado.end(), base.begin(), base.end());
    }

    return resultado;
}


/**
 * @brief Determina o tipo de solucao inicial baseado no nome da instancia.
 *
 * @param nome Nome do arquivo da instancia (ex: "H&Sconc200-2-6.txt").
 * @return Tipo de solucao inicial (0 a 6).
 */
int definirTipoSolucaoInicial(const string& nome)
{
    if (
        nome.find("prob") != string::npos ||
        nome.find("HSlin") != string::npos ||
        nome.find("HSconc") != string::npos
        ) {
        return 6; // Melhor entre as 6 estrategias iniciais
    }
    else if (nome.find("HSconv") != string::npos) {
        return 2; // FFD classico
    }
    else {
        cerr << "Tipo de instancia desconhecido: " << nome << endl;
        exit(EXIT_FAILURE);
    }
}


/**
 * @brief Determina o tempo limite de execucao com base no tamanho da instancia.
 *
 * @param nome Nome do arquivo da instancia (ex: "prob_500_3.txt").
 * @return Tempo limite em milissegundos.
 */
int definirTempoLimite(const string& nome)
{
    if (nome.find("25") != string::npos ||
        nome.find("50") != string::npos ||
        nome.find("100") != string::npos) {
        return 30000;
    }
    else if (nome.find("200") != string::npos) {
        return 15000;
    }
    else if (nome.find("500") != string::npos) {
        return 200000;
    }
    else if (nome.find("1000") != string::npos) {
        return 300000;
    }
    else if (nome.find("2000") != string::npos) {
        return 400000;
    }
    else {
        cerr << "Nao foi possivel definir TEMPO_LIMITE_MS para: " << nome << endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Funcao principal do programa VSBPP com Simulated Annealing e log de convergencia.
 *
 * Esta versao (v03j+) aplica o algoritmo Simulated Annealing com cinco modos de vizinhanca:
 * - Modo 1: troca o tipo de um bin por outro viavel;
 * - Modo 2: troca de itens entre dois bins;
 * - Modo 3: remove um bin se for possivel realocar seus itens;
 * - Modo 4: move um item de um bin para outro com capacidade;
 * - Modo 5: troca dois pares de itens entre dois bins.
 *
 * O programa executa as seguintes etapas para cada instancia:
 * - Leitura da instancia a partir de arquivo .txt;
 * - Geracao de sete solucoes iniciais: Aleatoria, Melhor de N, FFD classico e 3 variacoes, MinGlobal;
 * - Execucao do Simulated Annealing com parametros configuraveis;
 * - Registro de resultados finais (custo, tempo, taxa de ocupacao);
 * - Gravacao de log de convergencia em arquivo .csv e resumo em arquivo .txt.
 *
 * Arquivos gerados:
 * - 04.Resultados_SimulatedAnnealing_v03j+.txt (resumo geral)
 * - 04.v03j+.Convergencia_SA_<nome_instancia>.csv (log de temperatura e custos)
 *
 * @return 0 se a execucao terminar corretamente.
 * 
 * Este programa espera os seguintes 13 argumentos na linha de comando:
 * T0, alpha, SAmax, NUM_SOLUCOES_ALEATORIAS, NUM_EXECUCOES_SA,
 * NUM_TOP_K_TIPOS_BIN, K_TROCA_ITENS_BIN, CRITERIO_MELHOR_ENTRE_N,
 * LIMIAR_OCUPACAO_BIN, MAX_TENTATIVAS, SEED_FIXA, nomeInstancia_corrente
 */
int main(int argc, char* argv[]) {

    // Flag de controle: se false, nao imprime nada no terminal, apenas no arquivo de texto
    const bool debug = false;

    if (argc < 14) {
        cerr << "Uso: " << argv[0] << " <id.config> <id.instancia> <seed> <instancia> "
            << "<T0> <alpha> <SAmax> <NUM_SOLUCOES_ALEATORIAS> <NUM_TOP_K_TIPOS_BIN> "
            << "<K_TROCA_ITENS_BIN> <CRITERIO_MELHOR_ENTRE_N> <LIMIAR_OCUPACAO_BIN> <MAX_TENTATIVAS>\n";
        return 1;
    }

    // ===========================
    // PARSE DOS PARAMETROS DO iRACE
    // ===========================

    //Parse dos parâmetros no main()
    int id_config = stoi(argv[1]);
    int id_instancia = stoi(argv[2]);
    int SEED_FIXA = stoi(argv[3]);
    string nomeInstancia_corrente = argv[4];

    double T0 = stod(argv[5]);
    double alpha = stod(argv[6]);
    int SAmax = stoi(argv[7]);

    NUM_SOLUCOES_ALEATORIAS = stoi(argv[8]);
    NUM_TOP_K_TIPOS_BIN = stoi(argv[9]);
    K_TROCA_ITENS_BIN = stoi(argv[10]);
    CRITERIO_MELHOR_ENTRE_N = stoi(argv[11]);
    LIMIAR_OCUPACAO_BIN = stod(argv[12]);
    MAX_TENTATIVAS = stoi(argv[13]);

    // ===========================
    // SEED PARA GERADOR ALEATORIO
    // ===========================

    // Inicializa o gerador de numeros aleatorios PCG com a seed definida
    pcg32 rng(SEED_FIXA);

    // Lista de nomes de arquivos de instancias (sem extensao .txt)
    vector<string> nomes_instancias_1x = { nomeInstancia_corrente };


    // =======================================================
    // GERA LISTA FINAL DE INSTANCIAS PARA RODAR O EXPERIMENTO
    // =======================================================

    // Numero de repeticoes de cada instancia
    const int REPETICOES = 1;

    // Multiplica a lista base de nomes de instancias por REPETICOES    
    vector<string> nomes_instancias = gerarListaCombinada(nomes_instancias_1x, REPETICOES);
    
    // ============================
    // ARQUIVO GLOBAL DE RESULTADOS
    // ============================

    // Define o caminho do projeto (WSL)
    string caminho_Projeto = "./";

    // Gera nome único para o arquivo global de resultados com base nos IDs
    string nome_resultado_txt = caminho_Projeto + "Resultados/04.Resultados_SA_config" +
        to_string(id_config) + "_inst" + to_string(id_instancia) + ".txt";

    // Tenta abrir o arquivo de resultados
    ofstream output(nome_resultado_txt);

    if (!output.is_open()) {
        cerr << "Erro ao criar o arquivo de saída em: " << caminho_Projeto << endl;
        return 1;
    }

    // Loop principal: processa cada instancia
    for (size_t idx = 0; idx < nomes_instancias.size(); ++idx) {

        // Limpa estruturas globais antes de cada instancia
        cacheCustos.clear();            // limpa cache antes de processar nova instancia
        historicoHashes.clear();        // limpa historico de hashes visitados


        // ===========================
        // PARAMETROS DA INSTANCIA ATUAL
        // ===========================

        // Nome do arquivo da instancia atual (com extensao .txt)
        string nomeInstancia_corrente = nomes_instancias[idx];

        // Define o tipo de solucao inicial com base no nome:
        // - TIPO 6: melhor das 6 solucoes iniciais (para prob, H&Slin, H&Sconc)
        // - TIPO 2: FFD classico (para H&Sconv)
        int TIPO_SOLUCAO_INICIAL = definirTipoSolucaoInicial(nomeInstancia_corrente);

        // Define tempo limite com base no tamanho da instancia
        // - 25, 50, 100: 10s; 200: 30s; 500: 100s; 1000: 150s; 2000: 200s
        int TEMPO_LIMITE_MS = definirTempoLimite(nomeInstancia_corrente);


        // ============================
        // ARQUIVOS DAS INSTANCIAS
        // ============================

        // Monta caminho completo do arquivo da instancia, considerando caminhos absolutos como no iRace (ajustado para WSL)
        string txtInstancia_corrente;
        if (!nomeInstancia_corrente.empty() && nomeInstancia_corrente.front() == '/') {
            // Caminho absoluto fornecido pelo iRace
            txtInstancia_corrente = nomeInstancia_corrente;
        }
        else {
            // Caminho relativo local
            txtInstancia_corrente = caminho_Projeto + "instances/" + nomeInstancia_corrente;
        }
        // Leitura da instancia
        Instancia instancia_corrente;
        if (!lerInstancia(txtInstancia_corrente, instancia_corrente)) {
            cerr << "Erro ao ler a instancia: " << nomeInstancia_corrente << endl;
            continue;
        }


        // ============================
        // ARQUIVO DE CONVERGENCIA CSV
        // ============================

        // Define o caminho completo do arquivo de convergencia .csv para a instancia atual
        // Remove caminho e extensao do nome da instancia (ex: "instances/prob_200_6.txt" -> "prob_200_6")
        // Extrai id.config e id.instancia fornecidos pelo iRace (argv[1] e argv[2])
        // Ajuste para WSL
        string nome_arquivo_completo = nomeInstancia_corrente.substr(nomeInstancia_corrente.find_last_of("/\\") + 1); // remove caminho
        string nome_base = nome_arquivo_completo.substr(0, nome_arquivo_completo.find_last_of('.')); // remove extensao .txt
        string nome_csv = caminho_Projeto + "Resultados/04.v03j+.Convergencia_SA_config" +
            to_string(id_config) + "_inst" + to_string(id_instancia) + "_" + nome_base + ".csv";

        // Tenta abrir o arquivo CSV de convergencia
        ofstream log_convergencia(nome_csv);
        log_convergencia << "TempoTotal_ms,Temperatura,CustoMelhor,CustoAtual,IterTotal,"
            << "AceitacoesPiores,Modo1,Modo2,Modo3,Modo4,Modo5\n";

        // Gera duas solucoes iniciais para comparacao 
        Solucao solucao_aleatoria = construirSolucao(instancia_corrente, "aleatoria_viavel", rng);
        Solucao solucao_melhorAleatoria = construirSolucao(instancia_corrente, "melhor_de_N_aleatorias", rng);
        Solucao solucao_gulosaFFD = construirSolucao(instancia_corrente, "gulosa_FFD", rng);
        Solucao solucao_gulosaFFD_extremos = construirSolucao(instancia_corrente, "gulosa_FFD_extremos", rng);
        Solucao solucao_gulosaFFD_topKbaratos = construirSolucao(instancia_corrente, "gulosa_FFD_topKbaratos", rng);
        Solucao solucao_gulosaFFD_topKcaros = construirSolucao(instancia_corrente, "gulosa_FFD_topKcaros", rng);


        // Compara as seis solucoes iniciais e escolhe a de menor custo
        Solucao solucao_MinCost = min({ solucao_aleatoria, solucao_melhorAleatoria, solucao_gulosaFFD, solucao_gulosaFFD_extremos, solucao_gulosaFFD_topKbaratos, solucao_gulosaFFD_topKcaros },
            [](const Solucao& a, const Solucao& b) {
                return a.custo_total < b.custo_total;
            });


        // Escolhe solucao inicial com base na configuracao
        Solucao solucao_inicial;
        if (TIPO_SOLUCAO_INICIAL == 0) {
            solucao_inicial = solucao_aleatoria;
        }

        else if (TIPO_SOLUCAO_INICIAL == 1) {
            solucao_inicial = solucao_melhorAleatoria;
        }

        else if (TIPO_SOLUCAO_INICIAL == 2) {
            solucao_inicial = solucao_gulosaFFD;
        }

        else if (TIPO_SOLUCAO_INICIAL == 3) {
            solucao_inicial = solucao_gulosaFFD_extremos;
        }

        else if (TIPO_SOLUCAO_INICIAL == 4) {
            solucao_inicial = solucao_gulosaFFD_topKbaratos;
        }

        else if (TIPO_SOLUCAO_INICIAL == 5) {
            solucao_inicial = solucao_gulosaFFD_topKcaros;
        }

        else if (TIPO_SOLUCAO_INICIAL == 6) {
            solucao_inicial = solucao_MinCost;
        }

        // Variáveis adicionais para log
        int vizinhos_aceitos = 0;             // numero de vizinhos aceitos (melhores ou piores)
        int vizinhos_rejeitados = 0;          // numero de vizinhos rejeitados
        int solucoes_piores_aceitas = 0;      // numero de solucoes piores aceitas por Metropolis


        // Executa Simulated Annealing
        Solucao solucao_otimizada = executarSimulatedAnnealing(
            instancia_corrente, solucao_inicial, rng,
            SAmax, T0, alpha, TEMPO_LIMITE_MS,
            &log_convergencia,
            vizinhos_aceitos,
            vizinhos_rejeitados,
            solucoes_piores_aceitas);

        // Impressao no terminal (controlada por debug)
        /*
        cout << "================== 04.Simulated_Annealing_v03j+.cpp - " << TIPO_SOLUCAO_INICIAL
             << " - " << nomeInstancia_corrente + "_" + to_string(idx) << " ==================\n\n";

        if (debug) {
            cout << "Custo Aleatoria Viavel(0): " << solucao_aleatoria.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_aleatoria.tempo_execucao_ms << "\n";

            cout << "Custo Melhor N Aleatorias (1): " << solucao_melhorAleatoria.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_melhorAleatoria.tempo_execucao_ms << "\n";

            cout << "Custo Gulosa FFD (2): " << solucao_gulosaFFD.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD.tempo_execucao_ms << "\n";

            cout << "Custo FFD Extremos (3): " << solucao_gulosaFFD_extremos.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_extremos.tempo_execucao_ms << "\n";

            cout << "Custo FFD topKBaratos (4): " << solucao_gulosaFFD_topKbaratos.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_topKbaratos.tempo_execucao_ms << "\n";

            cout << "Custo FFD topKCaros (5): " << solucao_gulosaFFD_topKcaros.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_topKcaros.tempo_execucao_ms << "\n";

            cout << "Custo MinCost (6): " << solucao_MinCost.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_MinCost.tempo_execucao_ms << "\n\n";

            cout << "Iniciando Simulated Annealing com T0=" << T0
                << ", alpha=" << alpha << ", SAmax=" << SAmax
                << ", tempo limite=" << TEMPO_LIMITE_MS << " ms...\n";

            cout << "Custo Final (SA): " << solucao_otimizada.custo_total
                << " | Tempo Total (ms): " << fixed << setprecision(3) << solucao_otimizada.tempo_execucao_ms << "\n";

            cout << "Reducao de custo: " << solucao_inicial.custo_total
                << " -> " << solucao_otimizada.custo_total << "\n\n";

            // Calcula e imprime taxa média de ocupação
            double taxa_ocupacao = calcularTaxaOcupacao(solucao_otimizada, instancia_corrente);
            cout << "Taxa media de ocupacao: " << fixed << setprecision(4) << taxa_ocupacao << "\n\n";

        }
        */
        // Impressao no arquivo de texto (sempre gravado)
        output << "================== 04.Simulated_Annealing_v03j+.cpp - " << TIPO_SOLUCAO_INICIAL
            << " - " << nomeInstancia_corrente + "_" + to_string(idx) << " ==================\n";

        output << "Custo Aleatoria Viavel (0): " << solucao_aleatoria.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_aleatoria.tempo_execucao_ms << "\n";

        output << "Custo Melhor N Aleatorias (1): " << solucao_melhorAleatoria.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_melhorAleatoria.tempo_execucao_ms << "\n";

        output << "Custo Gulosa FFD (2): " << solucao_gulosaFFD.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD.tempo_execucao_ms << "\n";

        output << "Custo FFD Extremos (3): " << solucao_gulosaFFD_extremos.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_extremos.tempo_execucao_ms << "\n";

        output << "Custo FFD topKBaratos (4): " << solucao_gulosaFFD_topKbaratos.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_topKbaratos.tempo_execucao_ms << "\n";

        output << "Custo FFD topKCaros (5): " << solucao_gulosaFFD_topKcaros.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD_topKcaros.tempo_execucao_ms << "\n";

        output << "Custo MinCost (6): " << solucao_MinCost.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_MinCost.tempo_execucao_ms << "\n\n";

        output << "Iniciando Simulated Annealing com T0=" << T0
            << ", alpha=" << alpha << ", SAmax=" << SAmax
            << ", tempo limite=" << TEMPO_LIMITE_MS << " ms...\n";

        output << "Custo Final (SA): " << solucao_otimizada.custo_total
            << " | Tempo Total (ms): " << fixed << setprecision(3) << solucao_otimizada.tempo_execucao_ms << "\n";

        output << "Reducao de custo: " << solucao_inicial.custo_total
            << " -> " << solucao_otimizada.custo_total << "\n\n";


        // Calcula e imprime taxa média de ocupação
        output << "Taxa media de ocupacao: " << fixed << setprecision(4)
            << calcularTaxaOcupacao(solucao_otimizada, instancia_corrente) << "\n";


        // Contadores adicionais (passados como referencia ao final do SA, se preferir)
        output << "Total de vizinhos aceitos: " << vizinhos_aceitos << "\n";
        output << "Total de vizinhos rejeitados: " << vizinhos_rejeitados << "\n";
        output << "Solucoes piores aceitas (Metropolis): " << solucoes_piores_aceitas << "\n\n";

        // Forca gravacao imediata no arquivo TXT apos finalizar a instância atual
        output.flush();

        log_convergencia.close(); // Fecha o arquivo CSV de log de convergencia ao final da instancia

        cout << solucao_otimizada.custo_total << endl;
    }

    output.close();
    std::exit(0);
}
