// Add item form submit (updated logic)
document.getElementById('inventory-form').addEventListener('submit', async (e) => {
  e.preventDefault();

  const id = document.getElementById('item-id').value.trim();
  const name = document.getElementById('item-name').value.trim();
  const totalAdd = parseInt(document.getElementById('total-stock').value || '0', 10);
  const reorder = parseInt(document.getElementById('reorder-level').value || '0', 10);

  if (!id || !name) {
    showToast('Fill ID & name', 'error');
    return;
  }

  // Check if item already exists
  const existing = allItems.find(x => x.item_id.toLowerCase() === id.toLowerCase());

  if (existing) {
    // ITEM EXISTS → increase stock only
    const newStock = (existing.total_stock || 0) + totalAdd;

    const updatedItem = {
      ...existing,
      total_stock: newStock,
      reorder_level: reorder || existing.reorder_level
    };

    const res = await window.dataSdk.update(updatedItem);

    if (res.isOk) {
      showToast(`Stock updated: ${existing.item_name} → +${totalAdd} units`);
      document.getElementById('inventory-form').reset();
      await window.dataSdk.init(dataHandler);
    } else {
      showToast('Failed to update stock', 'error');
    }

  } else {
    // ITEM DOES NOT EXIST → create new
    const newItem = {
      item_id: id,
      item_name: name,
      total_stock: totalAdd,
      reorder_level: reorder,
      timestamp: new Date().toISOString()
    };

    const res = await window.dataSdk.create(newItem);

    if (res.isOk) {
      showToast('New item added');
      document.getElementById('inventory-form').reset();
      await window.dataSdk.init(dataHandler);
    } else {
      showToast('Failed to add new item', 'error');
    }
  }
});
