-- Adaptive Mesh Refinement (Quadtree) Benchmark
-- Generiert hohe Code-Komplexität und massive Heap-Fluktuationen

local MAX_DEPTH = 8
local THRESHOLD_SPLIT = 0.6
local THRESHOLD_MERGE = 0.2

local function create_node(x, y, size, depth)
    return {
        x = x, y = y, size = size, depth = depth,
        value = math.random(),
        children = nil -- Wenn unterteilt, enthält dies 4 Sub-Knoten
    }
end

local function split_node(node)
    if node.depth >= MAX_DEPTH then return end
    local hs = node.size / 2
    local d = node.depth + 1
    node.children = {
        create_node(node.x,      node.y,      hs, d),
        create_node(node.x + hs, node.y,      hs, d),
        create_node(node.x,      node.y + hs, hs, d),
        create_node(node.x + hs, node.y + hs, hs, d)
    }
end

local function merge_node(node)
    node.children = nil
    node.value = math.random()
end

-- Rekursive Aktualisierung der Baumtopologie (Provoziert Allokationsdynamik)
local function update_mesh(node, phase_shift)
    -- Dynamischer Schwellenwert simuliert Wellenbewegung über das Gitter
    local local_target = math.sin(node.x * 5 + phase_shift) * math.cos(node.y * 5 + phase_shift)
    local trigger = (node.value + local_target) / 2

    if node.children then
        local can_merge = true
        for i = 1, 4 do
            update_mesh(node.children[i], phase_shift)
            if node.children[i].children then
                can_merge = false
            end
        end
        if can_merge and trigger < THRESHOLD_MERGE then
            merge_node(node)
        end
    else
        if trigger > THRESHOLD_SPLIT then
            split_node(node)
            if node.children then
                for i = 1, 4 do
                    node.children[i].value = node.value
                end
            end
        end
    end
end

-- Traversierung zur Validierung und Berechnung von Prüfsummen
local function count_nodes(node)
    if not node.children then
        return 1, node.value
    end
    local count = 1
    local total_val = 0
    for i = 1, 4 do
        local c, v = count_nodes(node.children[i])
        count = count + c
        total_val = total_val + v
    end
    return count, total_val
end

-- Etablierter "Binary Trees" Benchmark mit integriertem Speicher-Monitoring

local function bottom_up_tree(depth)
    if depth > 0 then
        depth = depth - 1
        return { bottom_up_tree(depth), bottom_up_tree(depth) }
    else
        return { }
    end
end

local function item_check(tree)
    if tree[1] then
        return 1 + item_check(tree[1]) + item_check(tree[2])
    else
        return 1
    end
end

local function run_benchmark(N)
    local min_depth = 4
    local max_depth = math.max(min_depth + 2, N)
    
    print(string.format("%-25s %-20s", "Phase / Baumtiefe", "Heap-Größe (KB)"))
    print(string.rep("-", 45))
    print(string.format("%-25s %-20.2f", "Start (Basis)", collectgarbage("count")))

    -- Phase 1: Stretch Tree (Erzeugt instantan einen extremen Speicher-Peak)
    local stretch_depth = max_depth + 1
    local stretch_tree = bottom_up_tree(stretch_depth)
    print(string.format("Stretch Tree (Tiefe %d) %-5s %-20.2f", stretch_depth, "[PEAK]", collectgarbage("count")))
    
    -- Sofortige Freigabe des Stretch Trees
    stretch_tree = nil
    collectgarbage("collect")
    print(string.format("%-25s %-20.2f", "Nach Freigabe Stretch", collectgarbage("count")))

    -- Phase 2: Long-lived Tree (Setzt das permanente Speicher-Fundament)
    local long_lived_tree = bottom_up_tree(max_depth)
    print(string.format("Long-lived Tree (Tiefe %d) %-3s %-20.2f", max_depth, "[BASE]", collectgarbage("count")))

    -- Phase 3: Schleife mit wechselnden Baumtiefen und Iterationszahlen
    -- Hier entsteht die kontinuierliche Speicherfluktuation durch permanentes Erzeugen/Verwerfen.
    for depth = min_depth, max_depth, 2 do
        local iterations = 2 ^ (max_depth - depth + min_depth)
        
        for _ = 1, iterations do
            local temp_tree = bottom_up_tree(depth)
            -- Lokale Zuweisung bewirkt implizite Dereferenzierung im nächsten Schleifendurchlauf
        end
        
        -- Partieller GC-Schritt zur Abbildung realer Laufzeitbedingungen
        local before = collectgarbage("count")
        collectgarbage("step", 0)
        local after = collectgarbage("count")
        print(string.format("Schleife Tiefe %-10d %-20.2f %-20.2f", depth, collectgarbage("count"), before-after))
    end

    -- Phase 4: Finale Bereinigung
    long_lived_tree = nil
    collectgarbage("collect")
    print(string.format("%-25s %-20.2f", "Nach finalem Cleanup", collectgarbage("count")))
end

-- Parameter N bestimmt die maximale Baumtiefe und damit die Speicher-Skalierung.
-- N = 18 allokiert im Peak mehrere hundert Megabyte.
run_benchmark(19)
