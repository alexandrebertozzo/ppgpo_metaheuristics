/**
 * @file 05.Genetic_Algorithm_v03g+iRace.cpp
 *
 * @brief Algoritmo Genetico aplicado ao problema VSBPP (Variable Size Bin Packing Problem),
 * adaptado para tuning automatico de parametros via iRace.
 *
 * Esta versao (v03g+iRace) deriva da versao v03g+ e implementa um algoritmo genetico completo,
 * adaptativo e modular, agora com suporte total ao framework de otimizacao automatica iRace.
 * Os parametros principais do algoritmo sao passados via linha de comando, facilitando a
 * exploracao automatizada do espaco de configuracoes pelo iRace.
 *
 * @details
 * Principais componentes do algoritmo:
 * - Populacao inicial: hibrida, composta por solucoes aleatorias viaveis e FFD (First Fit Decreasing);
 * - Selecao: por ranking, priorizando individuos com menor custo total;
 * - Crossover: uniforme com correcao, alternando bins dos pais e evitando repeticao de itens;
 * - Mutacao: cinco modos de vizinhanca (SA v03j), com selecao adaptativa proporcional à eficacia
 *   de cada modo (total de usos e sucessos acumulados);
 * - Adaptacao da taxa de mutacao: dinamica, com teto superior e resfriamento progressivo.
 *   A taxa e aumentada se a diversidade for baixa ou houver estagnacao; e suavemente reduzida
 *   se a diversidade permanecer elevada por um periodo (controle por janela de iteracoes);
 * - Substituicao: com elitismo, preservando os 5 melhores individuos da geracao anterior;
 * - Critério de parada: tempo limite em milissegundos;
 * - Cache da funcao objetivo: evita recomputacoes via unordered_map<uint64_t, int>;
 * - Hash da solucao: usado para cache, controle de duplicatas e calculo de diversidade;
 * - Controle de duplicatas: via unordered_set para evitar repeticao de solucoes na populacao;
 * - Reinicializacao adaptativa: reinicializa uma fracao da populacao se a diversidade for baixa
 *   ou houver estagnacao prolongada;
 * - Monitoramento de diversidade: via calcularDiversidadePorHash(...), registrado no log;
 * - Reparo final com busca local (executarDescent): aplica Descent com vizinhancas 1 a 3
 *   sobre a melhor solucao encontrada, com limite de tempo e vizinhos por iteracao.
 *
 * Compatibilidade com iRace:
 * - Recebe os seguintes parametros diretamente da linha de comando (argc/argv):
 *     [1] taxa_mutacao_base
 *     [2] AUMENTO_TAXA_MUTACAO
 *     [3] LIMIAR_DIVERSIDADE
 *     [4] FRACAO_REINICIALIZAR
 *     [5] JANELA_ESTAGNACAO_MS
 * - A saida padrao imprime o custo total final (interpretable pelo iRace).
 *
 * Novas funcionalidades em relacao à versao v03g+:
 * - Entrada dinamica dos parametros via argv.
 * - Compatibilidade completa com scripts de tuning automatico (iRace).
 * - Adicao de teto superior para taxa de mutacao (MAX_TAXA_MUTACAO).
 * - Resfriamento adaptativo da taxa de mutacao quando diversidade se mantem elevada.
 *
 * Limitacoes:
 * - O reparo final e restrito a tempo curto e numero limitado de vizinhos.
 * - A busca local e aplicada somente à melhor solucao final.
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
unordered_set<uint64_t> historicoHashes; // conjunto para registrar hashes das soluções visitadas


// === Parametros para reinicializacao e adaptacao ===
const bool REINICIALIZACAO_ATIVA = true;                 // ativa ou nao a reinicializacao
int JANELA_ESTAGNACAO_MS;                               // tempo maximo sem melhora (em ms)  
double FRACAO_REINICIALIZAR;                            // fracao padrao a reinicializar
double LIMIAR_DIVERSIDADE;                              // diversidade abaixo da qual adaptamos
double AUMENTO_TAXA_MUTACAO;                            // acrescimo temporario na taxa de mutacao
double taxa_mutacao_base;                               // taxa base (default)

// Parametros adicionais fornecidos via iRace
double FATOR_POPULACAO = 1.0;
double PROB_CRUZAMENTO = 0.8;
int NUM_ELITES = 5;
double PCT_POPULACAO_ALEATORIAS = 0.80;


// Maximo numero de solução aleatórias
int NUM_SOLUCOES_ALEATORIAS = 64;

// ==============================
// PARAMETROS DE CONTROLE EXTRAS
// ==============================
const bool GA_MUTACAO_MULTIPLA_ATIVA = true; // Ativa perturbação múltipla de 1–5% dos bins
double GA_PERT_MIN_PCT = 0.01;               // Limite inferior percentual da perturbacao multipla (default 1%)
double GA_PERT_MAX_PCT = 0.05;               // Limite superior percentual da perturbacao multipla (default 5%)



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
 * @brief Struct de retorno do algoritmo genetico
 */
struct ResultadosGA {
    Solucao melhor;                                                   ///< melhor solucao encontrada
    int custo_inicial = 0;                                            ///< custo da solucao inicial (compatibilidade)
    double tempo_total_ms = 0.0;                                      ///< tempo total de execucao
    int total_filhos = 0;                                             ///< numero total de filhos gerados
    int total_mutados = 0;                                            ///< numero total de filhos mutados  
    int contagem_modos_mutacao[6];                                    ///< contagem de modos de mutacao (1 a 5)

    ResultadosGA() : contagem_modos_mutacao{ 0, 0, 0, 0, 0, 0 } {}    ///lista de inicialização no construtor.
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

        // Apenas adiciona o novo bin na solucao se o mesmo nao estiver vazio
        if (!novo_bin.itens.empty()) {
            solucao_corrente.bins.push_back(novo_bin);
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

    // Retorna a solucao de menor custo
    return *min_element(candidatas.begin(), candidatas.end(),
        [](const Solucao& a, const Solucao& b) {
            return a.custo_total < b.custo_total;
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
int escolherTipoBinViavel(const Instancia& instancia, int peso, int MODO_ESCOLHA_TIPO_BIN, pcg32& rng)
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

    // MODO 1 = sorteia entre o tipo mais barato e o mais caro entre os viaveis
    if (MODO_ESCOLHA_TIPO_BIN == 1) {
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo < instancia.tipos[b].custo;
            });
        uniform_int_distribution<int> d(0, 1); // 0 = mais barato, 1 = mais caro
        return viaveis[d(rng) * (viaveis.size() - 1)];
    }

    // MODO 2 = sorteia entre os 3 tipos mais caros entre os viaveis
    if (MODO_ESCOLHA_TIPO_BIN == 2) {
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo > instancia.tipos[b].custo;
            });
        int limite = min(3, static_cast<int>(viaveis.size()));
        uniform_int_distribution<int> d(0, limite - 1);
        return viaveis[d(rng)];
    }

    // MODO 3 = sorteia entre os 3 tipos mais baratos entre os viaveis
    if (MODO_ESCOLHA_TIPO_BIN == 3) {
        sort(viaveis.begin(), viaveis.end(), [&](int a, int b) {
            return instancia.tipos[a].custo < instancia.tipos[b].custo;
            });
        int limite = min(3, static_cast<int>(viaveis.size()));
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
 * @param modo Tipo de politica (0: FFD classico, 1: extremos, 2: top3caros, 3: top3baratos).
 * @return Solucao gerada conforme a politica de tipo.
 */
Solucao construirSolucaoFFDComPolitica(const Instancia& instancia_corrente, pcg32& rng, int MODO_ESCOLHA_TIPO_BIN)
{
    using namespace std::chrono;
    auto inicio = high_resolution_clock::now();

    Solucao solucao_corrente;

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
            int tipo = escolherTipoBinViavel(instancia_corrente, peso, MODO_ESCOLHA_TIPO_BIN, rng);
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
 * - "gulosa_FFD_top3caros": aplica FFD com tipo aleatorio entre os 3 mais caros viaveis;
 * - "gulosa_FFD_top3baratos": aplica FFD com tipo aleatorio entre os 3 mais baratos viaveis;
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
    else if (metodo == "gulosa_FFD_top3caros") {
        return construirSolucaoFFDComPolitica(instancia_corrente, rng, 2);
    }
    else if (metodo == "gulosa_FFD_top3baratos") {
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
 * @brief Gera apenas um vizinho aleatorio viavel a partir do modo informado.
 *
 * Esta funcao utiliza o modo de vizinhanca informado (1 a 5) e retorna o primeiro vizinho
 * valido gerado. Ideal para meta-heuristicas baseadas em amostragem estocastica como Simulated Annealing.
 *
 * A partir da versao SA v03j, o modo 1 realiza atualizacao incremental do custo:
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
        // MODO 5: troca dois pares de itens entre dois bins
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

                if (binA.itens.size() < 2 || binB.itens.size() < 2) continue;

                for (size_t i = 0; i < binA.itens.size(); ++i) {
                    for (size_t j = i + 1; j < binA.itens.size(); ++j) {
                        for (size_t k = 0; k < binB.itens.size(); ++k) {
                            for (size_t l = k + 1; l < binB.itens.size(); ++l) {

                                int ia = binA.itens[i], ja = binA.itens[j];
                                int ib = binB.itens[k], jb = binB.itens[l];

                                int capA = instancia.tipos[binA.tipo].capacidade;
                                int capB = instancia.tipos[binB.tipo].capacidade;

                                int cargaA = calcularCarga(binA, instancia);
                                int cargaB = calcularCarga(binB, instancia);

                                int deltaA = -instancia.itens[ia].peso - instancia.itens[ja].peso + instancia.itens[ib].peso + instancia.itens[jb].peso;
                                int deltaB = -instancia.itens[ib].peso - instancia.itens[jb].peso + instancia.itens[ia].peso + instancia.itens[ja].peso;

                                if (cargaA + deltaA <= capA && cargaB + deltaB <= capB) {
                                    Solucao v = solucao;
                                    auto& va = v.bins[a].itens;
                                    auto& vb = v.bins[b].itens;

                                    va.erase(remove(va.begin(), va.end(), ia), va.end());
                                    va.erase(remove(va.begin(), va.end(), ja), va.end());
                                    va.push_back(ib);
                                    va.push_back(jb);

                                    vb.erase(remove(vb.begin(), vb.end(), ib), vb.end());
                                    vb.erase(remove(vb.begin(), vb.end(), jb), vb.end());
                                    vb.push_back(ia);
                                    vb.push_back(ja);

                                    // os tipos nao mudaram
                                    v.custo_total = solucao.custo_total;

                                    return v;
                                }
                            }
                        }
                    }
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
 * @brief Executa busca local Descent para melhorar uma solucao final.
 *
 * Esta versao esta alinhada com as praticas do GA v03f+:
 * - Usa o cache global de custos (mapa hash_custos_total)
 * - Usa hash da solucao para evitar recomputacoes
 * - Combina vizinhancas (modos 1 a 3)
 * - Aplica Descent com criterio de melhora real (custo menor e solucao diferente)
 *
 * @param instancia Dados da instancia
 * @param solucao_inicial Solucao a ser refinada
 * @param rng Gerador aleatorio PCG
 * @param max_vizinhos Maximo de vizinhos por iteracao
 * @param tempo_limite_ms Tempo maximo permitido para refinamento
 * @return Melhor solucao encontrada
 */
Solucao executarDescent(const Instancia& instancia,
    Solucao solucao_inicial,
    pcg32& rng,
    int max_vizinhos,
    int tempo_limite_ms)
{
    using namespace chrono;

    auto inicio = high_resolution_clock::now();

    Solucao melhor = solucao_inicial;
    int melhor_custo = calcularCustoTotal(melhor, instancia);  // usa cache global
    bool melhorou = true;

    while (melhorou) {
        melhorou = false;

        auto agora = high_resolution_clock::now();
        if (duration_cast<milliseconds>(agora - inicio).count() > tempo_limite_ms)
            break;

        vector<Solucao> vizinhos;
        for (int modo = 1; modo <= 3; ++modo) {
            vector<Solucao> gerados = gerarMultiplosVizinhos(melhor, instancia, rng, max_vizinhos / 3, modo);
            vizinhos.insert(vizinhos.end(), gerados.begin(), gerados.end());
        }

        for (auto& vizinho : vizinhos) {
            int custo_viz = calcularCustoTotal(vizinho, instancia);

            if (custo_viz < melhor_custo && vizinho.bins != melhor.bins) {
                melhor = vizinho;
                melhor_custo = custo_viz;
                melhorou = true;
            }
        }
    }

    melhor.custo_total = melhor_custo;
    melhor.tempo_execucao_ms += duration<double, milli>(high_resolution_clock::now() - inicio).count();
    return melhor;
}

/**
 * @brief Gera a populacao inicial hibrida para o algoritmo genetico (GA).
 *
 * Esta funcao cria uma populacao inicial diversificada e sem duplicatas, com base na estrategia:
 * - 1 solucao deterministica (modo 0 - FFD classico)
 * - ~80% de solucoes aleatorias viaveis (modo -1)
 * - ~20% de solucoes gulosas aleatorizadas (modos 1, 2 e 3)
 *
 * Cada nova solucao tem seu hash calculado via FNV-1a, e apenas solucoes unicas sao adicionadas.
 * Essa abordagem garante diversidade inicial e evita clones na geracao base.
 *
 * @param instancia Instancia do problema VSBPP.
 * @param tamanho_populacao Numero total de individuos na populacao.
 * @param rng Gerador de numeros aleatorios (pcg32).
 * @return Vetor com a populacao inicial de solucoes unicas.
 */
vector<Solucao> gerarPopulacaoInicial(const Instancia& instancia,
    int tamanho_populacao,
    double PCT_POPULACAO_ALEATORIAS,
    pcg32& rng)
{
    vector<Solucao> populacao;
    populacao.reserve(tamanho_populacao);

    // Conjunto para evitar duplicacao por hash na populacao inicial
    unordered_set<uint64_t> historicoHashes_local;

    int qtd_aleatorias = PCT_POPULACAO_ALEATORIAS * tamanho_populacao;
    int qtd_restante = tamanho_populacao - qtd_aleatorias - 1;


    // Inclui 1 solucao deterministica usando modo 0 (FFD classico)
    Solucao s0 = construirSolucao(instancia, "gulosa_FFD", rng);
    uint64_t h0 = calcularHashSolucao(s0);
    populacao.push_back(s0);
    historicoHashes_local.insert(h0);


    const int MAX_TENTATIVAS_IGUAIS = 100;
    int tentativas_repetidas = 0;

    while (static_cast<int>(populacao.size()) < 1 + qtd_aleatorias && tentativas_repetidas < MAX_TENTATIVAS_IGUAIS) {
        Solucao nova = construirSolucao(instancia, "aleatoria_viavel", rng);
        uint64_t h = calcularHashSolucao(nova);
        if (historicoHashes_local.insert(h).second) {
            populacao.push_back(nova);  // adiciona se for unica
            tentativas_repetidas = 0;   // reseta contador
        }
        else {
            ++tentativas_repetidas;     // incrementa se repetida
        }
    }



    // variacoes gulosas com aleatoriedade (modos 1 a 3)
    tentativas_repetidas = 0; // reaproveita mesma variavel
    uniform_int_distribution<int> dist_modoguloso(1, 3);

    while (static_cast<int>(populacao.size()) < tamanho_populacao && tentativas_repetidas < MAX_TENTATIVAS_IGUAIS) {
        string modo_guloso;
        int sorteado = dist_modoguloso(rng);
        if (sorteado == 1) modo_guloso = "gulosa_FFD_extremos";
        if (sorteado == 2) modo_guloso = "gulosa_FFD_top3caros";
        if (sorteado == 3) modo_guloso = "gulosa_FFD_top3baratos";

        Solucao nova = construirSolucao(instancia, modo_guloso, rng);
        uint64_t h = calcularHashSolucao(nova);
        if (historicoHashes_local.insert(h).second) {
            populacao.push_back(nova);  // adiciona se for unica
            tentativas_repetidas = 0;
        }
        else {
            ++tentativas_repetidas;
        }
    }


    return populacao;
}



/**
 * @brief Gera um conjunto de novas solucoes unicas (por hash) para reinicializacao parcial.
 *
 * Esta funcao evita duplicatas em relacao aos hashes ja existentes na populacao.
 * Gera solucoes usando uma combinacao de modos aleatorios viaveis e gulosos.
 *
 * @param instancia Instancia do problema.
 * @param rng Gerador aleatorio PCG.
 * @param qtd Quantidade de novas solucoes a serem geradas.
 * @param hashes_existentes Conjunto com hashes de solucoes ja presentes.
 * @return Vetor de novas solucoes.
 */
vector<Solucao> gerarNovasSolucoes(const Instancia& instancia,
    pcg32& rng,
    int qtd,
    unordered_set<uint64_t>& hashes_existentes)
{
    vector<Solucao> novas;
    const int MAX_TENTATIVAS = 100 * qtd;
    int tentativas = 0;

    while (static_cast<int>(novas.size()) < qtd && tentativas < MAX_TENTATIVAS) {
        string modo;
        if (uniform_real_distribution<>(0, 1)(rng) < 0.5)
            modo = "aleatoria_viavel";
        else {
            int m = uniform_int_distribution<>(1, 3)(rng);
            if (m == 1) modo = "gulosa_FFD_extremos";
            if (m == 2) modo = "gulosa_FFD_top3caros";
            if (m == 3) modo = "gulosa_FFD_top3baratos";
        }

        Solucao nova = construirSolucao(instancia, modo, rng);
        uint64_t h = calcularHashSolucao(nova);

        if (hashes_existentes.insert(h).second) {
            nova.custo_total = calcularCustoTotal(nova, instancia);
            novas.push_back(nova);
        }

        ++tentativas;
    }

    return novas;
}


/**
 * @brief Seleciona pares de pais para cruzamento com base em ranking.
 *
 * Cada par e escolhido com preferencia para solucoes com menor custo.
 * O metodo evita selecionar o mesmo individuo duas vezes no mesmo par.
 *
 * @param populacao Vetor de solucoes candidatas.
 * @param rng Gerador de numeros aleatorios PCG.
 * @param num_pares Numero de pares a serem selecionados.
 * @return Vetor de pares de indices (pai1, pai2).
 */
vector<pair<int, int>> selecionarParesRanking(const vector<Solucao>& populacao, pcg32& rng, int num_pares) {
    vector<pair<int, int>> pares;
    int n = populacao.size();
    for (int i = 0; i < num_pares; ++i) {
        uniform_int_distribution<int> dist(0, n / 2); // favorece melhores
        int pai1 = dist(rng);
        int pai2 = dist(rng);
        while (pai2 == pai1) pai2 = dist(rng);
        pares.emplace_back(pai1, pai2);
    }
    return pares;
}

/**
 * @brief Executa crossover uniforme entre dois pais, evitando repeticao de itens.
 *
 * Alterna os bins entre pai1 e pai2, inserindo apenas itens ainda nao alocados.
 * Se houver itens nao alocados ao final, tenta aloca-los com estrategia gulosa FFD.
 *
 * @param pai1 Primeiro pai da recombinacao.
 * @param pai2 Segundo pai da recombinacao.
 * @param instancia Instancia do problema.
 * @param rng Gerador de numeros aleatorios PCG.
 * @return Solucao resultante do cruzamento.
 */

Solucao crossoverUniformeCorrigido(const Solucao& pai1, const Solucao& pai2, const Instancia& instancia, pcg32& rng) {
    unordered_set<int> usados;
    Solucao filha;

    size_t max_bins = max(pai1.bins.size(), pai2.bins.size());
    for (size_t i = 0; i < max_bins; ++i) {
        const Solucao& pai = (i % 2 == 0) ? pai1 : pai2;
        if (i >= pai.bins.size()) continue;

        BinUsado novo = pai.bins[i];
        vector<int> itens_validos;

        for (int item : novo.itens) {
            if (usados.find(item) == usados.end()) {
                usados.insert(item);
                itens_validos.push_back(item);
            }
        }

        if (!itens_validos.empty()) {
            novo.itens = itens_validos;
            filha.bins.push_back(novo);
        }
    }

    // Aloca itens restantes de forma gulosa
    for (int i = 0; i < instancia.n; ++i) {
        if (usados.count(i) == 0) {
            Solucao parcial = construirSolucao(instancia, "gulosa_FFD", rng);
            for (const BinUsado& bin : parcial.bins) {
                for (int item : bin.itens) {
                    if (usados.count(item) == 0) {
                        filha.bins.push_back(bin);
                        for (int usado : bin.itens) usados.insert(usado);
                        break;
                    }
                }
            }
        }
    }

    filha.custo_total = calcularCustoTotal(filha, instancia);
    return filha;
}


/**
 * @brief Aplica uma mutacao aleatoria em um dos 5 modos.
 * Incrementa a contagem se a mutacao for bem-sucedida.
 *
 * @param sol Solucao original.
 * @param instancia Instancia do problema.
 * @param rng Gerador de numeros aleatorios.
 * @param contagem_modos_mutacao Vetor de contagem dos modos (indice 1 a 5).
 * @return Solucao modificada (ou original se mutacao falhou).
 */
Solucao executarMutacao(const Solucao& sol, const Instancia& instancia, pcg32& rng, int* contagem_modos_mutacao) {
    uniform_int_distribution<int> dist_modo(1, 5); // sorteia modo entre 1 e 5
    int modo = dist_modo(rng); // seleciona um modo aleatorio

    // Aplica a mutacao usando o modo sorteado
    Solucao vizinho = gerarUnicoVizinho(sol, instancia, rng, modo);

    // Se a mutacao realmente alterou a solucao
    if (vizinho.custo_total != sol.custo_total) {
        ++contagem_modos_mutacao[modo]; // incrementa contagem do modo
        return vizinho;
    }
    else {
        return sol; // mutacao nao teve efeito
    }
}


/**
 * @brief Aplica multiplas perturbacoes na solucao, modificando 1 a 5% dos bins.
 *        Utiliza o mesmo modo base da mutacao adaptativa.
 *
 * @param sol Solucao original.
 * @param instancia Instancia do problema.
 * @param rng Gerador de numeros aleatorios.
 * @param modo_base Modo base de mutacao (1 a 5).
 * @return Solucao perturbada.
 */
Solucao executarMutacaoMultipla(const Solucao& sol, const Instancia& instancia, pcg32& rng, int modo_base) {
    Solucao pert = sol;

    int num_bins = static_cast<int>(pert.bins.size());
    if (num_bins == 0) return pert;

    uniform_real_distribution<double> dist_pct(GA_PERT_MIN_PCT, GA_PERT_MAX_PCT);
    int num_alteracoes = max(1, static_cast<int>(ceil(dist_pct(rng) * num_bins)));

    for (int i = 0; i < num_alteracoes; ++i) {
        pert = gerarUnicoVizinho(pert, instancia, rng, modo_base);
    }

    return pert;
}


/**
 * @brief Executa mutacao adaptativa com base na eficacia historica de cada modo.
 *
 * Modos com melhor desempenho historico (mais sucesso ao melhorar o custo)
 * recebem maior probabilidade de selecao. Nenhum modo e excluido completamente.
 *
 * @param sol Solucao original.
 * @param instancia Instancia do problema.
 * @param rng Gerador de numeros aleatorios.
 * @param contagem_modos_mutacao Vetor de contagem por modo (1 a 5).
 * @param total_uso_modo Total de vezes que cada modo foi usado.
 * @param total_sucesso_modo Total de vezes que cada modo melhorou a solucao.
 * @return Nova solucao apos mutacao (ou original se nenhuma melhoria ocorreu).
 */
Solucao executarMutacaoAdaptativa(const Solucao& sol, const Instancia& instancia,
    pcg32& rng, int* contagem_modos_mutacao,
    vector<int>& total_uso_modo,
    vector<int>& total_sucesso_modo)
{
    vector<double> pesos(6, 1.0); // pesos de 1 a 5 (ignora indice 0)

    // calcula eficacia relativa e ajusta pesos
    for (int modo = 1; modo <= 5; ++modo) {
        if (total_uso_modo[modo] > 0) {
            pesos[modo] = 1.0 + static_cast<double>(total_sucesso_modo[modo]) / total_uso_modo[modo];
        }
    }

    // sorteia modo proporcional aos pesos
    discrete_distribution<int> dist_modo(pesos.begin() + 1, pesos.end());
    int modo = dist_modo(rng) + 1;

    total_uso_modo[modo]++;
    Solucao vizinho;
    if (GA_MUTACAO_MULTIPLA_ATIVA) {
        vizinho = executarMutacaoMultipla(sol, instancia, rng, modo);
    }
    else {
        vizinho = gerarUnicoVizinho(sol, instancia, rng, modo);
    }


    if (vizinho.custo_total < sol.custo_total) {
        total_sucesso_modo[modo]++;
        ++contagem_modos_mutacao[modo];
        return vizinho;
    }
    else {
        return sol;
    }
}


/**
 * @brief Substitui a populacao aplicando elitismo.
 *
 * Mantem os n_elite melhores individuos da populacao atual
 * e preenche o restante com os melhores filhos gerados.
 *
 * @param populacao Populacao atual (antes da substituicao).
 * @param filhos Vetor de solucoes filhas geradas por cruzamento/mutacao.
 * @param n_elite Numero de melhores individuos a manter fixos.
 * @return Nova populacao apos substituicao.
 */

vector<Solucao> substituirGeracaoComElitismo(vector<Solucao>& populacao, vector<Solucao>& filhos, int n_elite) {
    sort(populacao.begin(), populacao.end(), [](const Solucao& a, const Solucao& b) {
        return a.custo_total < b.custo_total;
        });

    sort(filhos.begin(), filhos.end(), [](const Solucao& a, const Solucao& b) {
        return a.custo_total < b.custo_total;
        });

    vector<Solucao> nova_populacao;
    for (int i = 0; i < n_elite; ++i)
        nova_populacao.push_back(populacao[i]);

    for (size_t i = 0; i < filhos.size() && nova_populacao.size() < populacao.size(); ++i)
        nova_populacao.push_back(filhos[i]);

    return nova_populacao;
}

/**
 * @brief Calcula o custo medio da populacao fornecida.
 *
 * @param populacao Vetor de solucoes representando a populacao atual.
 * @return Custo medio da populacao (media aritmetica dos custos individuais).
 */
double calcularCustoMedioPopulacao(const vector<Solucao>& populacao, const Instancia& instancia)
{
    if (populacao.empty()) return 0.0;

    int soma = 0;
    for (const Solucao& sol : populacao) {

        soma += calcularCustoTotal(sol, instancia);

    }

    return static_cast<double>(soma) / populacao.size();
}


/**
 * @brief Calcula a diversidade da populacao com base em hashes unicos.
 *
 * Esta funcao avalia a diversidade da populacao usando o numero de hashes
 * distintos das solucoes, considerando a funcao de hash FNV-1a aplicada
 * sobre a estrutura completa da solucao.
 *
 * Quanto maior o numero de hashes unicos, maior a diversidade estrutural da populacao.
 *
 * @param populacao Vetor de solucoes atuais na populacao.
 * @return Numero de solucoes distintas baseado em hash.
 */
int calcularDiversidadePorHash(const vector<Solucao>& populacao) {
    unordered_set<uint64_t> hashes_unicos;

    for (const Solucao& sol : populacao) {
        hashes_unicos.insert(calcularHashSolucao(sol));
    }

    return static_cast<int>(hashes_unicos.size());
}


/**
 * @brief Executa o algoritmo genetico com tempo limite e estrategias adaptativas.
 *
 * Esta versao v03g++ inclui:
 * - Mutacao adaptativa proporcional à eficacia dos modos;
 * - Reparo final com busca local (Descent);
 * - Monitoramento da diversidade da populacao via hash;
 * - Reajuste dinamico da taxa de mutacao com teto superior;
 * - Resfriamento da taxa de mutacao quando a diversidade melhora;
 * - Reinicializacao proporcional à diversidade quando combinada com estagnacao temporal.
 *
 * Gera uma populacao inicial hibrida, aplica selecao, crossover,
 * mutacao e substituicao com elitismo ate atingir o tempo limite.
 */

ResultadosGA executarAlgoritmoGenetico(const Instancia& instancia,
    const vector<Solucao>& populacao_inicial,
    pcg32& rng,
    int tempo_limite_ms,
    int tamanho_populacao,
    ofstream* log_convergencia,
    unordered_set<uint64_t>& historicoHashes,
    double PROB_CRUZAMENTO,
    int NUM_ELITES,
    double PCT_POPULACAO_ALEATORIAS)
{
    auto inicio = chrono::steady_clock::now();
    auto ultimo_melhora = inicio; // marco temporal para detectar estagnacao


    double taxa_mutacao = taxa_mutacao_base;      // valor dinamico que pode ser ajustado
    const double taxa_crossover = PROB_CRUZAMENTO;
    const int num_elites = NUM_ELITES;

    vector<Solucao> populacao = populacao_inicial; // reutiliza populacao gerada no main

    // encontrar a melhor solucao inicial da populacao
    Solucao solucao_inicial = *min_element(populacao.begin(), populacao.end(),
        [](const Solucao& a, const Solucao& b) {
            return a.custo_total < b.custo_total;
        });

    Solucao melhor = *min_element(populacao.begin(), populacao.end(), [](const Solucao& a, const Solucao& b) {
        return a.custo_total < b.custo_total;
        });

    // Vetores de contagem e eficacia dos modos de mutacao
    ResultadosGA resultado;
    int* contagem_modos_mutacao = resultado.contagem_modos_mutacao;
    vector<int> total_uso_modo(6, 0);       // quantas vezes cada modo foi usado (1..5)
    vector<int> total_sucesso_modo(6, 0);   // quantas vezes gerou solucao melhor


    int total_filhos = 0;
    int total_mutados = 0;
    int total_reinicializacoes = 0; // contador acumulado de reinicializacoes


    const double MAX_TAXA_MUTACAO = 0.30;
    const double MIN_TAXA_MUTACAO = 0.01;
    const int N_ITERACOES_PARA_RESFRIAMENTO = 30;
    int iteracoes_diversidade_ok = 0;
    bool resfriamento_ocorreu = false;


    while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - inicio).count() < tempo_limite_ms) {
        vector<Solucao> filhos;
        auto pares = selecionarParesRanking(populacao, rng, tamanho_populacao / 2);

        for (size_t k = 0; k < pares.size(); ++k) {
            int i = pares[k].first;
            int j = pares[k].second;
            if (uniform_real_distribution<>(0, 1)(rng) < taxa_crossover) {
                Solucao f1 = crossoverUniformeCorrigido(populacao[i], populacao[j], instancia, rng);
                Solucao f2 = crossoverUniformeCorrigido(populacao[j], populacao[i], instancia, rng);

                historicoHashes.insert(calcularHashSolucao(f1));
                historicoHashes.insert(calcularHashSolucao(f2));

                filhos.push_back(f1);
                filhos.push_back(f2);

                total_filhos += 2;
            }
        }

        for (Solucao& filho : filhos) {
            if (uniform_real_distribution<>(0, 1)(rng) < taxa_mutacao) {

                filho = executarMutacaoAdaptativa(filho, instancia, rng,
                    contagem_modos_mutacao, total_uso_modo, total_sucesso_modo);

                historicoHashes.insert(calcularHashSolucao(filho));
                total_mutados++;
            }
        }

        populacao = substituirGeracaoComElitismo(populacao, filhos, num_elites);

        Solucao atual_melhor = *min_element(populacao.begin(), populacao.end(), [](const Solucao& a, const Solucao& b) {
            return a.custo_total < b.custo_total;
            });

        if (atual_melhor.custo_total < melhor.custo_total) {
            melhor = atual_melhor;
            ultimo_melhora = chrono::steady_clock::now(); // reseta estagnacao
        }


        // calcula diversidade da geracao
        int diversidade = calcularDiversidadePorHash(populacao);

        // calcula tempo desde a ultima melhora
        auto agora = chrono::steady_clock::now();
        int tempo_desde_melhora = chrono::duration_cast<chrono::milliseconds>(agora - ultimo_melhora).count();

        // === ADAPTACAO DA TAXA DE MUTACAO ===
        if (diversidade < LIMIAR_DIVERSIDADE || tempo_desde_melhora > JANELA_ESTAGNACAO_MS) {

            taxa_mutacao = min(taxa_mutacao_base + AUMENTO_TAXA_MUTACAO, MAX_TAXA_MUTACAO);
        }
        else {
            iteracoes_diversidade_ok++;
            if (iteracoes_diversidade_ok >= N_ITERACOES_PARA_RESFRIAMENTO) {
                taxa_mutacao = max(taxa_mutacao * 0.9, MIN_TAXA_MUTACAO);
                iteracoes_diversidade_ok = 0;
                resfriamento_ocorreu = true;
            }
        }

        // === REINICIALIZACAO PARCIAL DA POPULACAO ===
        if (REINICIALIZACAO_ATIVA &&
            (diversidade < LIMIAR_DIVERSIDADE || tempo_desde_melhora > JANELA_ESTAGNACAO_MS)) {

            int qtd_reinicializar = static_cast<int>(populacao.size() * FRACAO_REINICIALIZAR);

            // Coleta hashes existentes
            unordered_set<uint64_t> hashes_existentes;
            for (const Solucao& s : populacao)
                hashes_existentes.insert(calcularHashSolucao(s));

            // Gera novas solucoes
            vector<Solucao> novos = gerarNovasSolucoes(instancia, rng, qtd_reinicializar, hashes_existentes);

            // Ordena e substitui os piores
            sort(populacao.begin(), populacao.end(), [](const Solucao& a, const Solucao& b) {
                return a.custo_total < b.custo_total;
                });

            for (int i = 0; i < static_cast<int>(novos.size()); ++i)
                populacao[populacao.size() - 1 - i] = novos[i];

            total_reinicializacoes++; // incrementa contador acumulado
            ultimo_melhora = agora;   // reseta estagnacao
        }


        // calcula o custo medio da populacao antes de registrar no log
        double custo_medio = calcularCustoMedioPopulacao(populacao, instancia);


        int flag_resfriamento = (resfriamento_ocorreu ? 1 : 0);


        // Log de convergencia
        if (log_convergencia) {
            auto agora = chrono::steady_clock::now();
            double tempo_ms = chrono::duration<double, milli>(agora - inicio).count();

            *log_convergencia << tempo_ms << ","                             // tempo decorrido
                << melhor.custo_total << ","                   // custo da melhor solucao
                << atual_melhor.custo_total << ","             // custo atual
                << populacao.size() << ","                     // tamanho da populacao
                << total_filhos << ","                         // total de filhos
                << total_mutados << ","                        // total de mutacoes
                << custo_medio << ","
                << contagem_modos_mutacao[1] << ","
                << contagem_modos_mutacao[2] << ","
                << contagem_modos_mutacao[3] << ","
                << contagem_modos_mutacao[4] << ","
                << contagem_modos_mutacao[5] << ","
                << total_reinicializacoes << ","               // nova coluna no log
                << diversidade << ","                        // diversidade da geracao
                << taxa_mutacao << ","
                << flag_resfriamento << ","
                << total_uso_modo[1] << "," << total_sucesso_modo[1] << ","
                << total_uso_modo[2] << "," << total_sucesso_modo[2] << ","
                << total_uso_modo[3] << "," << total_sucesso_modo[3] << ","
                << total_uso_modo[4] << "," << total_sucesso_modo[4] << ","
                << total_uso_modo[5] << "," << total_sucesso_modo[5] << "\n";

        }

    }

    // Marca o tempo antes da busca local
    auto antes_descent = chrono::steady_clock::now();

    // Reparo final com busca local (Descent)
    melhor = executarDescent(instancia, melhor, rng, 30, 100); // Ex: 30 vizinhos, 100ms

    // Soma o tempo da busca local ao tempo total
    melhor.tempo_execucao_ms = chrono::duration<double, milli>(chrono::steady_clock::now() - inicio).count();

    resultado.melhor = melhor;
    resultado.custo_inicial = solucao_inicial.custo_total;
    resultado.tempo_total_ms = melhor.tempo_execucao_ms; // tempo ja medido dentro da funcao
    resultado.total_filhos = total_filhos;
    resultado.total_mutados = total_mutados;
    for (int i = 1; i <= 5; ++i)
        resultado.contagem_modos_mutacao[i] = contagem_modos_mutacao[i]; // copia acumulado


    return resultado;

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
 * @brief Determina o tempo limite de execucao com base no tamanho da instancia.
 *
 * @param nome Nome do arquivo da instancia (ex: "prob_500_3.txt").
 * @return Tempo limite em milissegundos.
 */
int definirTempoLimite(const string& nome)
{
    if (nome.find("2000") != string::npos) {
        return 200000;
    }
    else if (nome.find("1000") != string::npos) {
        return 150000;
    }
    else if (nome.find("500") != string::npos) {
        return 100000;
    }
    else if (nome.find("200") != string::npos) {
        return 100000;
    }
    if (nome.find("25") != string::npos ||
        nome.find("50") != string::npos ||
        nome.find("100") != string::npos) {
        return 60000;
    }
    else {
        cerr << "Nao foi possivel definir TEMPO_LIMITE_MS para: " << nome << endl;
        exit(EXIT_FAILURE);
    }
}

/**
 * @brief Define o tamanho da populacao inicial com base no nome da instancia.
 *
 * Usa faixas de tamanho de instancias extraidas do nome do arquivo para ajustar dinamicamente
 * o tamanho da populacao, equilibrando diversidade e custo computacional.
 *
 * @param nome Nome do arquivo da instancia (ex: "prob_500_3.txt").
 * @return Tamanho da populacao inicial.
 */
int definirTamanhoPopulacao(const string& nome)
{
    if (nome.find("2000") != string::npos) {
        return 400;
    }
    else if (nome.find("1000") != string::npos) {
        return 300;
    }
    else if (nome.find("500") != string::npos) {
        return 250;
    }
    else if (nome.find("200") != string::npos) {
        return 200;
    }
    else if (nome.find("25") != string::npos ||
             nome.find("50") != string::npos ||
             nome.find("100") != string::npos) {
        return 150;
    }
    else {
        cerr << "Nao foi possivel definir TAMANHO_POPULACAO para: " << nome << endl;
        exit(EXIT_FAILURE);
    }
}


/**
 * @brief Funcao principal do programa VSBPP com algoritmo genetico (GA v03g+).
 *
 * Esta versao aplica o algoritmo genetico ao problema VSBPP utilizando codificacao
 * por vetor de bins. A solucao e representada por um conjunto de bins com os itens
 * alocados, evitando violacoes de capacidade. O processo envolve as seguintes etapas:
 *
 * - Leitura da instancia a partir de arquivo .txt;
 * - Geracao de sete solucoes iniciais, incluindo: aleatoria, melhor de N, FFD padrao,
 *   FFD com variacoes e MinGlobal;
 * - Execucao do algoritmo genetico com populacao hibrida (aleatoria e gulosa);
 * - Aplicacao de selecao por ranking, crossover uniforme corrigido, mutacao por
 *   vizinhancas e substituicao com elitismo;
 * - Registro do custo final, tempo de execucao e numero de bins usados;
 * - Gravacao do resumo final em arquivo .txt e do log de convergencia em .csv.
 *
 * O algoritmo genetico implementa os seguintes componentes:
 * - Codificacao: vetor de inteiros indicando o bin de cada item (via estrutura Solucao);
 * - Populacao inicial: 50% aleatoria viavel, 50% gulosa (FFD);
 * - Funcao objetivo: minimizar o custo total da solucao;
 * - Selecao: baseada em ranking da populacao ordenada por custo;
 * - Crossover: uniforme com alternancia de bins dos pais, sem repetir itens;
 * - Mutacao: baseada nos cinco modos de vizinhanca do SA v03j:
 *   (1) troca de tipo de bin, (2) troca de itens entre bins,
 *   (3) remoçao de bin com realocacao, (4) mover item entre bins,
 *   (5) troca de pares de itens entre bins;
 * - Substituicao: elitismo com 5 melhores individuos mantidos por geracao;
 * - Criterio de parada: tempo limite (em milissegundos).
 *
 * Parametros do algoritmo:
 * - Tamanho da populacao: 100 individuos;
 * - Taxa de crossover: 0.8;
 * - Taxa de mutacao: 0.05;
 * - Numero de filhos por cruzamento: 2;
 * - Numero de elites mantidos: 5.
 *
 * Arquivos gerados:
 * - 05.Resultados_Genetic_Algorithm_v03g+.txt : resumo da melhor solucao e metadados;
 * - 05.v03g+.Convergencia_GA_<nome_instancia>.csv : log de convergencia por geracao.
 *
 * @return 0 se a execucao ocorrer com sucesso.
 */

int main(int argc, char* argv[]) {


    if (argc < 5) {
        cerr << "Uso: " << argv[0] << " <id.config> <id.instancia> <seed> <instancia> [--param value ...]\n";
        return 1;
    }

    // Ignora os três primeiros argumentos (iRace): id.config, id.instancia, seed
    int idx_param = 4;

    // Nome da instância (vem logo após os 3 argumentos iniciais)
    string nomeInstancia_corrente = argv[idx_param++];


    // ===========================
    // PARSE DOS PARAMETROS DO iRACE
    // ===========================
    // Leitura posicional dos parâmetros enviados pelo iRace
    taxa_mutacao_base = stod(argv[idx_param++]);
    AUMENTO_TAXA_MUTACAO = stod(argv[idx_param++]);
    LIMIAR_DIVERSIDADE = stod(argv[idx_param++]);
    FRACAO_REINICIALIZAR = stod(argv[idx_param++]);
    JANELA_ESTAGNACAO_MS = stoi(argv[idx_param++]);
    FATOR_POPULACAO = stod(argv[idx_param++]);
    PROB_CRUZAMENTO = stod(argv[idx_param++]);
    NUM_ELITES = stoi(argv[idx_param++]);
    PCT_POPULACAO_ALEATORIAS = stod(argv[idx_param++]);


    // Novos parâmetros do iRace: limites de perturbação múltipla (1% a 5%, típicos)
    GA_PERT_MIN_PCT = stod(argv[idx_param++]);  ///< Ex: 0.01
    GA_PERT_MAX_PCT = stod(argv[idx_param++]);  ///< Ex: 0.05


    // Flag de controle: se false, nao imprime nada no terminal nem no arquivo de texto
    const bool debug = false;


    // ===========================
    // SEED PARA GERADOR ALEATORIO
    // ===========================
    unsigned SEED_FIXA = static_cast<unsigned>(stoul(argv[3])); // usa seed fornecida pelo iRace em argv[3]

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


    // Extrai id.config e id.instancia fornecidos pelo iRace (argv[1] e argv[2])
    int id_config = stoi(argv[1]);
    int id_instancia = stoi(argv[2]);

    // Define o caminho do projeto (WSL)
    string caminho_Projeto = "./";

    // Gera nome único para o arquivo global de resultados com base nos IDs
    string nome_resultado_txt = caminho_Projeto + "Resultados/05.Resultados_GA_config" +
        to_string(id_config) + "_inst" + to_string(id_instancia) + ".txt";

    // Abre arquivo de resultados
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

        // Define tempo limite e tamanho da população com base no tamanho da instancia
        // 25, 50, 100: (10s,100); 200: (30s,300); 500: (60s,500); 1000: (150s,700); 2000: (200s,1000)
        int TEMPO_LIMITE_MS = definirTempoLimite(nomeInstancia_corrente);
        int TAMANHO_POPULACAO = static_cast<int>(FATOR_POPULACAO * definirTamanhoPopulacao(nomes_instancias[idx]));


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
        string nome_csv = caminho_Projeto + "Resultados/05.v03g+.Convergencia_GA_config" +
            to_string(id_config) + "_inst" + to_string(id_instancia) + "_" + nome_base + ".csv";


        // Tenta abrir o arquivo CSV de convergencia
        ofstream log_convergencia(nome_csv);
        log_convergencia << "Tempo,MelhorCusto,CustoAtual,Populacao,TotalFilhos,TotalMutacoes,CustoMedio,"
            "ModoMutacao1, ModoMutacao2, ModoMutacao3, ModoMutacao4, ModoMutacao5, reinicializacoes, Diversidade,taxaMutacao, flagResfriamento,"
            "UsoModo1,SucessoModo1,UsoModo2,SucessoModo2,UsoModo3,SucessoModo3,UsoModo4,SucessoModo4,UsoModo5,SucessoModo5,\n";


        // Gera duas solucoes iniciais para comparacao 
        Solucao solucao_melhorAleatoria = construirSolucao(instancia_corrente, "melhor_de_N_aleatorias", rng);
        Solucao solucao_gulosaFFD = construirSolucao(instancia_corrente, "gulosa_FFD", rng);

        // Marca inicio da rodada completa (como no SA v03j)
        auto inicio_rodada = chrono::high_resolution_clock::now();

        // Executa Genetic Algorithm
        // Gera populacao inicial e mede tempo
        auto t0 = chrono::high_resolution_clock::now();
        vector<Solucao> populacao = gerarPopulacaoInicial(instancia_corrente, TAMANHO_POPULACAO, PCT_POPULACAO_ALEATORIAS, rng);


        // registra hashes das solucoes iniciais
        for (const auto& s : populacao) {
            historicoHashes.insert(calcularHashSolucao(s));
        }


        auto t1 = chrono::high_resolution_clock::now();


        // Executa algoritmo genetico
        ResultadosGA solucao_ga = executarAlgoritmoGenetico(instancia_corrente, populacao, rng, TEMPO_LIMITE_MS, TAMANHO_POPULACAO,
            &log_convergencia, historicoHashes, PROB_CRUZAMENTO, NUM_ELITES, PCT_POPULACAO_ALEATORIAS);



        auto t2 = chrono::high_resolution_clock::now();

        double tempo_populacao_ms = chrono::duration<double, milli>(t1 - t0).count();

        // Marca fim da rodada completa
        auto fim_rodada = chrono::high_resolution_clock::now();

        // Atualiza tempo total real da rodada, consistente com SA v03j
        solucao_ga.tempo_total_ms = chrono::duration<double, milli>(fim_rodada - inicio_rodada).count();



        // Impressao no terminal (controlada por debug)
        /*
        cout << "================== 05.Genetic_Algorithm_v03g+.cpp - "
            << " - " << nomeInstancia_corrente << " ==================\n";

        if (debug) {

            cout << "Custo Melhor N Aleatorias (1): " << solucao_melhorAleatoria.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_melhorAleatoria.tempo_execucao_ms << "\n";

            cout << "Custo Gulosa FFD (2): " << solucao_gulosaFFD.custo_total
                << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD.tempo_execucao_ms << "\n";

            cout << "Custo Final (GA): " << solucao_ga.melhor.custo_total
                << " | Tempo Total (ms): " << fixed << setprecision(3) << solucao_ga.melhor.tempo_execucao_ms << "\n";


            // Resultados GA
            cout << "Custo total da solucao GA: " << solucao_ga.melhor.custo_total << "\n";
            cout << "Reducao de custo: " << solucao_ga.custo_inicial << " -> " << solucao_ga.melhor.custo_total << "\n";
            cout << "Tempo total de execucao (ms): " << solucao_ga.tempo_total_ms << "\n";
            cout << "Total de filhos gerados: " << solucao_ga.total_filhos << "\n";
            cout << "Filhos apos mutacao: " << solucao_ga.total_mutados << "\n\n";

            cout << "Contagem de modos de mutacao aplicados com sucesso:\n";
            for (int m = 1; m <= 5; ++m)
                cout << "Modo " << m << ": " << solucao_ga.contagem_modos_mutacao[m] << "\n";


            // Calcula e imprime taxa média de ocupação
            double taxa_ocupacao = calcularTaxaOcupacao(solucao_ga.melhor, instancia_corrente);
            cout << "Taxa media de ocupacao: " << fixed << setprecision(4) << taxa_ocupacao << "\n\n";

        }
        */
        // Impressao no arquivo de texto (sempre gravado)
        output << "================== 05.Genetic_Algorithm_v03g+.cpp - "
            << " - " << nomeInstancia_corrente << " ==================\n";

        output << "Custo Melhor N Aleatorias (1): " << solucao_melhorAleatoria.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_melhorAleatoria.tempo_execucao_ms << "\n";

        output << "Custo Gulosa FFD (2): " << solucao_gulosaFFD.custo_total
            << " | Tempo (ms): " << fixed << setprecision(3) << solucao_gulosaFFD.tempo_execucao_ms << "\n";

        // Resultados GA
        output << "Custo total da solucao GA: " << solucao_ga.melhor.custo_total << "\n";
        output << "Reducao de custo: " << solucao_ga.custo_inicial << " -> " << solucao_ga.melhor.custo_total << "\n";
        output << "Tempo total de execucao (ms): " << solucao_ga.tempo_total_ms << "\n";
        output << "Total de filhos gerados: " << solucao_ga.total_filhos << "\n";
        output << "Filhos apos mutacao: " << solucao_ga.total_mutados << "\n\n";

        output << "Contagem de modos de mutacao aplicados com sucesso:\n";
        for (int m = 1; m <= 5; ++m)
            output << "Modo " << m << ": " << solucao_ga.contagem_modos_mutacao[m] << "\n";

        // Calcula e imprime taxa média de ocupação
        output << "Taxa media de ocupacao: " << fixed << setprecision(4)
            << calcularTaxaOcupacao(solucao_ga.melhor, instancia_corrente) << "\n\n";

        log_convergencia.close(); // Fecha o arquivo CSV de log de convergencia ao final da instancia

        cout << solucao_ga.melhor.custo_total << endl;

    }

    output.close();
    std::exit(0);
}


