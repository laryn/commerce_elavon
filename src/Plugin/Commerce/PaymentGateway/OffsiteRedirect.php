<?php

namespace Drupal\commerce_elavon\Plugin\Commerce\PaymentGateway;

use Drupal\commerce_order\Entity\OrderInterface;
use Drupal\commerce_payment\Plugin\Commerce\PaymentGateway\OffsitePaymentGatewayBase;
use Drupal\Core\Form\FormStateInterface;
use Symfony\Component\HttpFoundation\Request;

/**
 * Provides the Off-site Redirect payment gateway.
 *
 * @CommercePaymentGateway(
 *   id = "elavon_offsite_redirect",
 *   label = "Elavon (Off-site redirect)",
 *   display_label = "Elavon Offsite",
 *   forms = {
 *     "offsite-payment" = "Drupal\commerce_elavon\PluginForm\OffsiteRedirect\PaymentOffsiteForm",
 *   },
 *   payment_method_types = {"credit_card"},
 *   credit_card_types = {
 *     "amex", "dinersclub", "discover", "jcb", "maestro", "mastercard", "visa",
 *   },
 * )
 */
class OffsiteRedirect extends OffsitePaymentGatewayBase {

  /**
   * {@inheritdoc}
   */
  public function defaultConfiguration() {
    return [
      'multicurrency' => FALSE,
    ] + parent::defaultConfiguration();
  }

  /**
   * {@inheritdoc}
   */
  public function buildConfigurationForm(array $form, FormStateInterface $form_state) {
    $form = parent::buildConfigurationForm($form, $form_state);

    // A real gateway would always know which redirect method should be used,
    // it's made configurable here for test purposes.
    $form['merchant_id'] = [
      '#type' => 'textfield',
      '#title' => $this->t('Merchant Id'),
      '#default_value' => $this->configuration['merchant_id'],
      '#required' => TRUE,
    ];

    $form['user_id'] = [
      '#type' => 'textfield',
      '#title' => $this->t('Merchant User Id'),
      '#default_value' => $this->configuration['user_id'],
      '#required' => TRUE,
    ];

    $form['pin'] = [
      '#type' => 'textfield',
      '#title' => $this->t('Merchant Pin'),
      '#default_value' => $this->configuration['pin'],
      '#required' => TRUE,
    ];

    $form['multicurrency'] = [
      '#type' => 'radios',
      '#title' => $this->t('Multi-Currency support'),
      '#description' => $this->t('Use only with a terminal that is setup with Multi-Currency.'),
      '#options' => [
        TRUE => $this->t('Support Multi-Currency'),
        FALSE => $this->t('Do Not Support'),
      ],
      '#default_value' => (int) $this->configuration['multicurrency'],
    ];

    return $form;
  }

  /**
   * {@inheritdoc}
   */
  public function submitConfigurationForm(array &$form, FormStateInterface $form_state) {
    parent::submitConfigurationForm($form, $form_state);
    if (!$form_state->getErrors()) {
      $values = $form_state->getValue($form['#parents']);
      $this->configuration['merchant_id'] = $values['merchant_id'];
      $this->configuration['user_id'] = $values['user_id'];
      $this->configuration['pin'] = $values['pin'];
      $this->configuration['multicurrency'] = $values['multicurrency'];
    }
  }

  /**
   * {@inheritdoc}
   */
  public function onReturn(OrderInterface $order, Request $request) {
    // @todo Add examples of request validation.
    $payment_storage = $this->entityTypeManager->getStorage('commerce_payment');
    $result = $request->query->get('ssl_result_message');
    $state = ($result == 'APPROVAL') ? 'Completed' : $result;
    $payment = $payment_storage->create([
      'state' => $state,
      'amount' => $order->getTotalPrice(),
      'payment_gateway' => $this->entityId,
      'order_id' => $order->id(),
      'remote_id' => $request->query->get('ssl_txn_id'),
      'remote_state' => $request->query->get('ssl_result'),
    ]);
    $payment->save();
    drupal_set_message('Payment was processed');
  }

}
