<?php

/**
 * @file
 * Administrative forms for the Commerce Elavon module.
 */


/**
 * Form callback: allows the user to capture a prior authorization.
 */
function commerce_elavon_capture_form($form, &$form_state, $order, $transaction) {
  $form_state['order'] = $order;
  $form_state['transaction'] = $transaction;

  // Load and store the payment method instance for this transaction.
  $payment_method = commerce_payment_method_instance_load($transaction->instance_id);
  $form_state['payment_method'] = $payment_method;

  $balance = commerce_payment_order_balance($order);

  if ($balance['amount'] > 0 && $balance['amount'] < $transaction->amount) {
    $default_amount = $balance['amount'];
  }
  else {
    $default_amount = $transaction->amount;
  }

  // Convert the price amount to a user friendly decimal value.
  $default_amount = number_format(commerce_currency_amount_to_decimal($default_amount, $transaction->currency_code), 2, '.', '');

  $description = implode('<br />', array(
    t('Authorization: @amount', array('@amount' => commerce_currency_format($transaction->amount, $transaction->currency_code))),
    t('Order balance: @balance', array('@balance' => commerce_currency_format($balance['amount'], $balance['currency_code']))),
  ));

  $form['amount'] = array(
    '#type' => 'textfield',
    '#title' => t('Capture amount'),
    '#description' => $description,
    '#default_value' => $default_amount,
    '#field_suffix' => check_plain($transaction->currency_code),
    '#size' => 16,
  );

  $form = confirm_form($form,
    t('What amount do you want to capture?'),
    'admin/commerce/orders/' . $order->order_id . '/payment',
    '',
    t('Capture'),
    t('Cancel'),
    'confirm'
  );

  return $form;
}

/**
 * Validate handler: ensure a valid amount is given.
 */
function commerce_elavon_capture_form_validate($form, &$form_state) {
  $transaction = $form_state['transaction'];
  $amount = $form_state['values']['amount'];

  // Ensure a positive numeric amount has been entered for capture.
  if (!is_numeric($amount) || $amount <= 0) {
    form_set_error('amount', t('You must specify a positive numeric amount to capture.'));
  }

  // Ensure the amount is less than or equal to the authorization amount.
  if ($amount > commerce_currency_amount_to_decimal($transaction->amount, $transaction->currency_code)) {
    form_set_error('amount', t('You cannot capture more than you authorized.'));
  }

  // If the authorization has expired, display an error message and redirect.
  if (time() - $transaction->created > 86400 * 10) {
    drupal_set_message(t('This authorization has passed its 10 days limit cannot be captured.'), 'error');
    drupal_goto('admin/commerce/orders/' . $form_state['order']->order_id . '/payment');
  }
}

/**
 * Submit handler: process a prior authorization capture via AIM.
 */
function commerce_elavon_capture_form_submit($form, &$form_state) {
  $transaction = $form_state['transaction'];
  $amount = number_format($form_state['values']['amount'], 2, '.', '');
  $payment_method = $form_state['payment_method'];
  $order = $form_state['order'];
  $info = array(
    'ssl_transaction_type' => 'cccomplete',
    'ssl_txn_id' => $transaction->remote_id,
    'ssl_amount' => $amount,
  );
  $response = commerce_elavon_request($payment_method, $info);
  $transaction->payload[REQUEST_TIME] = isset($response['xml']) ? $response['xml']->asXML() : '';
  $transaction->remote_status = $response['status'];

  if ($response['status']) {
    drupal_set_message(t('Prior authorization captured successfully.'));

    // Update the transaction amount to the actual capture amount.
    $transaction->amount = commerce_currency_decimal_to_amount($amount, $transaction->currency_code);
    $transaction->status = COMMERCE_PAYMENT_STATUS_SUCCESS;
    $transaction->message .= '<br />' . t('Captured: @date', array('@date' => format_date(REQUEST_TIME, 'short')));
  }
  else {
    drupal_set_message(t('Prior authorization capture failed, so the transaction will remain in a pending status.'), 'error');
    drupal_set_message(check_plain((string) $response['xml']->errorMessage), 'error');
  }

  commerce_payment_transaction_save($transaction);
  $form_state['redirect'] = 'admin/commerce/orders/' . $order->order_id . '/payment';
}

/**
 * Form callback: allows the user to issue a refund on a prior transaction.
 */
function commerce_elavon_direct_refund_form($form, &$form_state, $order, $transaction) {
  $form_state['order'] = $order;
  $form_state['transaction'] = $transaction;

  // Load and store the payment method instance for this transaction.
  $payment_method = commerce_payment_method_instance_load($transaction->instance_id);
  $form_state['payment_method'] = $payment_method;

  $default_amount = number_format(commerce_currency_amount_to_decimal($transaction->amount, $transaction->currency_code), 2, '.', '');

  $form['amount'] = array(
    '#type' => 'textfield',
    '#title' => t('Credit amount'),
    '#description' => t('Enter the amount to be credited back to the original credit card.'),
    '#default_value' => $default_amount,
    '#field_suffix' => check_plain($transaction->currency_code),
    '#size' => 16,
  );

  $form = confirm_form($form,
    t('What amount do you want to refund?'),
    'admin/commerce/orders/' . $order->order_id . '/payment',
    '',
    t('Credit'),
    t('Cancel'),
    'confirm'
  );

  return $form;
}

/**
 * Validate handler: check the refund amount before attempting refund request.
 */
function commerce_elavon_direct_refund_form_validate($form, &$form_state) {
  $transaction = $form_state['transaction'];
  $amount = $form_state['values']['amount'];

  // Ensure a positive numeric amount has been entered for refund.
  if (!is_numeric($amount) || $amount <= 0) {
    form_set_error('amount', t('You must specify a positive numeric amount to refund.'));
  }

  // Ensure the amount is less than or equal to the captured amount.
  if ($amount > commerce_currency_amount_to_decimal($transaction->amount, $transaction->currency_code)) {
    form_set_error('amount', t('You cannot refund more than you captured.'));
  }
}

/**
 * Submit handler: process a refund via Elavon direct.
 */
function commerce_elavon_direct_refund_form_submit($form, &$form_state) {
  $transaction = $form_state['transaction'];
  $amount = number_format($form_state['values']['amount'], 2, '.', '');
  $order = $form_state['order'];
  $payment_method = $form_state['payment_method'];

  $info = array(
    'ssl_transaction_type' => 'ccreturn',
    'ssl_amount' => $amount,
    'ssl_txn_id' => $transaction->remote_id,
  );
  $response = commerce_elavon_request($payment_method, $info);

  if ($response['status']) {
    $refund_amount = commerce_currency_decimal_to_amount($amount, $transaction->currency_code);
    drupal_set_message(t('Refund for @amount issued successfully', array('@amount' => commerce_currency_format($refund_amount, $transaction->currency_code))));

    // Create a new transaction to record the refund.
    $refund_transaction = commerce_payment_transaction_new('elavon_direct', $order->order_id);
    $refund_transaction->instance_id = $payment_method['instance_id'];
    $refund_transaction->remote_id = isset($response['xml']->ssl_txn_id) ? (string) $response['xml']->ssl_txn_id : '';
    $refund_transaction->amount = $refund_amount * -1;
    $refund_transaction->currency_code = $transaction->currency_code;
    $refund_transaction->payload[REQUEST_TIME] = isset($response['xml']) ? $response['xml']->asXML() : '';
    $refund_transaction->status = COMMERCE_PAYMENT_STATUS_SUCCESS;
    $refund_transaction->remote_status = $response['status'];
    $refund_transaction->message = t('Refunded to @remote_id.', array('@remote_id' => $transaction->remote_id));

    // Save the refund transaction.
    commerce_payment_transaction_save($refund_transaction);
  }
  else {
    // Save the failure response message to the original transaction.
    $transaction->payload[REQUEST_TIME] = isset($response['xml']) ? $response['xml']->asXML() : '';

    // Display a failure message and response reason from Elavon.
    drupal_set_message(t('Refund failed: @reason', array('@reason' => (string) $response['xml']->errorMessage)), 'error');

    commerce_payment_transaction_save($transaction);
  }
  $form_state['redirect'] = 'admin/commerce/orders/' . $order->order_id . '/payment';
}

/**
 * Form callback: allows the user to issue a ccdelete on a prior transaction.
 */
function commerce_elavon_direct_delete_form($form, &$form_state, $order, $transaction) {
  $form_state['order'] = $order;
  $form_state['transaction'] = $transaction;

  $form['help'] = array(
    '#markup' => t('The ccdelete is a transaction that deletes and attempts a reversal on a Sale or Auth Only Credit transaction.'),
  );
  $form['help']['#markup'] .= ' ' . t('A transaction that has been deleted from the batch cannot be recovered.');
  // Load and store the payment method instance for this transaction.
  $payment_method = commerce_payment_method_instance_load($transaction->instance_id);
  $form_state['payment_method'] = $payment_method;

  $form = confirm_form($form,
    t('Are you sure you want to perform a ccdelete on that transaction?'),
    'admin/commerce/orders/' . $order->order_id . '/payment',
    '',
    t('Confirm'),
    t('Cancel'),
    'confirm'
  );

  return $form;
}

/**
 * Submit handler: process a ccdelete via Elavon direct.
 */
function commerce_elavon_direct_delete_form_submit($form, &$form_state) {
  $transaction = $form_state['transaction'];
  $order = $form_state['order'];
  $payment_method = $form_state['payment_method'];

  $info = array(
    'ssl_transaction_type' => 'ccdelete',
    'ssl_txn_id' => $transaction->remote_id,
  );
  $response = commerce_elavon_request($payment_method, $info);
  $transaction->payload[REQUEST_TIME] = isset($response['xml']) ? $response['xml']->asXML() : '';
  $transaction->remote_status = $response['status'];

  if ($response['status']) {
    drupal_set_message(t('ccdelete issued successfully'));
    $transaction->status = COMMERCE_PAYMENT_STATUS_FAILURE;
    $transaction->message .= '<br />' . t('Ccdelete performed: @date', array('@date' => format_date(REQUEST_TIME, 'short')));
  }
  else {
    drupal_set_message(t('ccdelete failed: @reason', array('@reason' => (string) $response['xml']->errorMessage)), 'error');
  }

  commerce_payment_transaction_save($transaction);
  $form_state['redirect'] = 'admin/commerce/orders/' . $order->order_id . '/payment';
}
